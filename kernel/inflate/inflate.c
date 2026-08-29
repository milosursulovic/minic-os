// DEFLATE (RFC 1951) decompression - stored, fixed-Huffman, and
// dynamic-Huffman blocks, LZ77 back-references. All tables below (fixed
// Huffman code lengths, length/distance base+extra-bits, the code-length
// alphabet's own transmission order) are RFC-spec constants, not someone
// else's code - the same category as embedding a well-known CRC
// polynomial.

#include "inflate.h"

#define MAX_BITS 15
#define MAX_LIT_SYMBOLS 288
#define MAX_DIST_SYMBOLS 30
#define MAX_CL_SYMBOLS 19

typedef struct {
    const u8* data;
    u32 len;
    u32 byte_pos;
    u8 bit_pos;
    bool error;
} bitreader;

static u32 br_getbit(bitreader* br) {
    if (br->byte_pos >= br->len) {
        br->error = true;
        return 0;
    }
    u32 bit = (br->data[br->byte_pos] >> br->bit_pos) & 1u;
    br->bit_pos = br->bit_pos + 1;
    if (br->bit_pos == 8) {
        br->bit_pos = 0;
        br->byte_pos = br->byte_pos + 1;
    }
    return bit;
}

static u32 br_getbits(bitreader* br, u32 n) {
    u32 v = 0;
    u32 i = 0;
    while (i < n) {
        v = v | (br_getbit(br) << i);
        i = i + 1;
    }
    return v;
}

static void br_align(bitreader* br) {
    if (br->bit_pos != 0) {
        br->bit_pos = 0;
        br->byte_pos = br->byte_pos + 1;
    }
}

// Canonical Huffman table, built from an array of per-symbol code
// lengths (RFC1951 3.2.2): count[len] = how many symbols use that code
// length, symbol[] = symbols sorted by (length, then code value).
typedef struct {
    int count[MAX_BITS + 1];
    int symbol[MAX_LIT_SYMBOLS];
} huffman_table;

static void huffman_build(huffman_table* h, const u8* lengths, u32 num_symbols) {
    for (int i = 0; i <= MAX_BITS; i = i + 1) {
        h->count[i] = 0;
    }
    for (u32 i = 0; i < num_symbols; i = i + 1) {
        h->count[lengths[i]] = h->count[lengths[i]] + 1;
    }
    h->count[0] = 0;

    int offsets[MAX_BITS + 2];
    offsets[1] = 0;
    for (int len = 1; len <= MAX_BITS; len = len + 1) {
        offsets[len + 1] = offsets[len] + h->count[len];
    }
    for (u32 i = 0; i < num_symbols; i = i + 1) {
        if (lengths[i] != 0) {
            h->symbol[offsets[lengths[i]]] = (int) i;
            offsets[lengths[i]] = offsets[lengths[i]] + 1;
        }
    }
}

// Standard canonical-Huffman bit-by-bit decode: walk one bit at a time,
// tracking the first code and table index at each length, until the
// current code falls within the range of codes that have that length.
static int huffman_decode(huffman_table* h, bitreader* br) {
    int code = 0;
    int first = 0;
    int index = 0;
    for (int len = 1; len <= MAX_BITS; len = len + 1) {
        code = code | (int) br_getbit(br);
        if (br->error) {
            return -1;
        }
        int count = h->count[len];
        if (code - first < count) {
            return h->symbol[index + (code - first)];
        }
        index = index + count;
        first = first + count;
        first = first << 1;
        code = code << 1;
    }
    return -1;
}

static const u16 LENGTH_BASE[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
static const u8 LENGTH_EXTRA[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};
static const u16 DIST_BASE[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
};
static const u8 DIST_EXTRA[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};
static const u8 CL_ORDER[19] = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
};

static void build_fixed_tables(huffman_table* lit, huffman_table* dist) {
    u8 lit_lengths[MAX_LIT_SYMBOLS];
    for (u32 i = 0; i < 144; i = i + 1) lit_lengths[i] = 8;
    for (u32 i = 144; i < 256; i = i + 1) lit_lengths[i] = 9;
    for (u32 i = 256; i < 280; i = i + 1) lit_lengths[i] = 7;
    for (u32 i = 280; i < 288; i = i + 1) lit_lengths[i] = 8;
    huffman_build(lit, lit_lengths, MAX_LIT_SYMBOLS);

    u8 dist_lengths[MAX_DIST_SYMBOLS];
    for (u32 i = 0; i < MAX_DIST_SYMBOLS; i = i + 1) dist_lengths[i] = 5;
    huffman_build(dist, dist_lengths, MAX_DIST_SYMBOLS);
}

static bool read_dynamic_tables(bitreader* br, huffman_table* lit, huffman_table* dist) {
    u32 hlit = br_getbits(br, 5) + 257;
    u32 hdist = br_getbits(br, 5) + 1;
    u32 hclen = br_getbits(br, 4) + 4;
    if (br->error) {
        return false;
    }

    u8 cl_lengths[MAX_CL_SYMBOLS] = {0};
    for (u32 i = 0; i < hclen; i = i + 1) {
        cl_lengths[CL_ORDER[i]] = (u8) br_getbits(br, 3);
    }
    if (br->error) {
        return false;
    }
    huffman_table cl_table;
    huffman_build(&cl_table, cl_lengths, MAX_CL_SYMBOLS);

    u8 lengths[MAX_LIT_SYMBOLS + MAX_DIST_SYMBOLS];
    u32 total = hlit + hdist;
    if (total > MAX_LIT_SYMBOLS + MAX_DIST_SYMBOLS) {
        return false;
    }
    u32 n = 0;
    while (n < total) {
        int sym = huffman_decode(&cl_table, br);
        if (sym < 0) {
            return false;
        }
        if (sym < 16) {
            lengths[n] = (u8) sym;
            n = n + 1;
        } else if (sym == 16) {
            if (n == 0) {
                return false;
            }
            u32 rep = br_getbits(br, 2) + 3;
            u8 prev = lengths[n - 1];
            while (rep > 0 && n < total) {
                lengths[n] = prev;
                n = n + 1;
                rep = rep - 1;
            }
        } else if (sym == 17) {
            u32 rep = br_getbits(br, 3) + 3;
            while (rep > 0 && n < total) {
                lengths[n] = 0;
                n = n + 1;
                rep = rep - 1;
            }
        } else {
            u32 rep = br_getbits(br, 7) + 11;
            while (rep > 0 && n < total) {
                lengths[n] = 0;
                n = n + 1;
                rep = rep - 1;
            }
        }
        if (br->error) {
            return false;
        }
    }

    huffman_build(lit, lengths, hlit);
    huffman_build(dist, &lengths[hlit], hdist);
    return true;
}

static bool inflate_block_stored(bitreader* br, u8* dst, u32 dst_cap, u32* dst_pos) {
    br_align(br);
    if (br->byte_pos + 4 > br->len) {
        return false;
    }
    u32 lenv = (u32) br->data[br->byte_pos] | ((u32) br->data[br->byte_pos + 1] << 8);
    u32 nlen = (u32) br->data[br->byte_pos + 2] | ((u32) br->data[br->byte_pos + 3] << 8);
    if (((~lenv) & 0xFFFFu) != nlen) {
        return false;
    }
    br->byte_pos = br->byte_pos + 4;
    if (br->byte_pos + lenv > br->len) {
        return false;
    }
    if (*dst_pos + lenv > dst_cap) {
        return false;
    }
    for (u32 i = 0; i < lenv; i = i + 1) {
        dst[*dst_pos] = br->data[br->byte_pos];
        *dst_pos = *dst_pos + 1;
        br->byte_pos = br->byte_pos + 1;
    }
    return true;
}

static bool inflate_block_huffman(bitreader* br, huffman_table* lit, huffman_table* dist,
                                   u8* dst, u32 dst_cap, u32* dst_pos) {
    while (true) {
        int sym = huffman_decode(lit, br);
        if (sym < 0) {
            return false;
        }
        if (sym < 256) {
            if (*dst_pos >= dst_cap) {
                return false;
            }
            dst[*dst_pos] = (u8) sym;
            *dst_pos = *dst_pos + 1;
        } else if (sym == 256) {
            return true;
        } else {
            u32 lidx = (u32)(sym - 257);
            if (lidx >= 29) {
                return false;
            }
            u32 length = LENGTH_BASE[lidx] + br_getbits(br, LENGTH_EXTRA[lidx]);
            int dsym = huffman_decode(dist, br);
            if (dsym < 0 || dsym >= 30) {
                return false;
            }
            u32 distance = DIST_BASE[dsym] + br_getbits(br, DIST_EXTRA[dsym]);
            if (br->error) {
                return false;
            }
            if (distance > *dst_pos) {
                return false;
            }
            if (*dst_pos + length > dst_cap) {
                return false;
            }
            u32 src_pos = *dst_pos - distance;
            for (u32 i = 0; i < length; i = i + 1) {
                dst[*dst_pos] = dst[src_pos];
                *dst_pos = *dst_pos + 1;
                src_pos = src_pos + 1;
            }
        }
    }
}

bool inflate(const u8* src, u32 src_len, u8* dst, u32 dst_cap, u32* out_len) {
    bitreader br;
    br.data = src;
    br.len = src_len;
    br.byte_pos = 0;
    br.bit_pos = 0;
    br.error = false;

    u32 dst_pos = 0;
    bool final = false;
    while (!final) {
        if (br.error) {
            return false;
        }
        final = br_getbit(&br) != 0;
        u32 btype = br_getbits(&br, 2);
        if (br.error) {
            return false;
        }

        if (btype == 0) {
            if (!inflate_block_stored(&br, dst, dst_cap, &dst_pos)) {
                return false;
            }
        } else if (btype == 1 || btype == 2) {
            huffman_table lit_table;
            huffman_table dist_table;
            if (btype == 1) {
                build_fixed_tables(&lit_table, &dist_table);
            } else {
                if (!read_dynamic_tables(&br, &lit_table, &dist_table)) {
                    return false;
                }
            }
            if (!inflate_block_huffman(&br, &lit_table, &dist_table, dst, dst_cap, &dst_pos)) {
                return false;
            }
        } else {
            return false;
        }
    }

    *out_len = dst_pos;
    return true;
}
