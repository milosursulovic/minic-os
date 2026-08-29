// PNG chunk parsing, zlib unwrap, scanline defiltering, pixel unpack -
// see png.h for scope. CRC32 table/polynomial and the zlib Adler32
// algorithm are both standard, well-known constants/algorithms, hand
// implemented here (not ported code).

#include "png.h"
#include "../../inflate/inflate.h"
#include "../../mm/heap/heap.h"

#define PNG_SIG_LEN 8
static const u8 PNG_SIGNATURE[PNG_SIG_LEN] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

static bool chunk_type_is(const u8* type, char a, char b, char c, char d) {
    return type[0] == (u8) a && type[1] == (u8) b && type[2] == (u8) c && type[3] == (u8) d;
}

static u32 read_u32be(const u8* p) {
    return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | (u32) p[3];
}

// --- CRC32 (standard reflected CRC-32/PNG, polynomial 0xEDB88320) ---

static u32 g_crc_table[256];
static bool g_crc_table_ready;

static void crc32_init(void) {
    if (g_crc_table_ready) {
        return;
    }
    for (u32 n = 0; n < 256; n = n + 1) {
        u32 c = n;
        for (int k = 0; k < 8; k = k + 1) {
            if (c & 1u) {
                c = 0xEDB88320u ^ (c >> 1);
            } else {
                c = c >> 1;
            }
        }
        g_crc_table[n] = c;
    }
    g_crc_table_ready = true;
}

static u32 crc32_of(const u8* data, u32 len) {
    crc32_init();
    u32 c = 0xFFFFFFFFu;
    for (u32 i = 0; i < len; i = i + 1) {
        c = g_crc_table[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

// --- Adler32 (zlib stream checksum) ---

static u32 adler32_of(const u8* data, u32 len) {
    u32 a = 1;
    u32 b = 0;
    for (u32 i = 0; i < len; i = i + 1) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

// --- PNG scanline defilter (spec 9.2-9.3) ---

static u8 paeth_predictor(u8 a, u8 b, u8 c) {
    int p = (int) a + (int) b - (int) c;
    int pa = p - (int) a;
    if (pa < 0) pa = -pa;
    int pb = p - (int) b;
    if (pb < 0) pb = -pb;
    int pc = p - (int) c;
    if (pc < 0) pc = -pc;
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

static bool defilter(u8* raw, u32 width, u32 height, u32 bpp) {
    u32 stride = bpp * width;
    for (u32 y = 0; y < height; y = y + 1) {
        u8* row = &raw[y * (1 + stride)];
        u8 filter_type = row[0];
        u8* prow = &row[1];
        u8* up_row = (y == 0) ? (u8*) 0 : &raw[(y - 1) * (1 + stride) + 1];
        for (u32 x = 0; x < stride; x = x + 1) {
            u8 a = (x >= bpp) ? prow[x - bpp] : 0;
            u8 b = up_row ? up_row[x] : 0;
            u8 c = (up_row && x >= bpp) ? up_row[x - bpp] : 0;
            u8 raw_val = prow[x];
            u8 recon;
            if (filter_type == 0) {
                recon = raw_val;
            } else if (filter_type == 1) {
                recon = (u8) (raw_val + a);
            } else if (filter_type == 2) {
                recon = (u8) (raw_val + b);
            } else if (filter_type == 3) {
                recon = (u8) (raw_val + (((u32) a + (u32) b) / 2));
            } else if (filter_type == 4) {
                recon = (u8) (raw_val + paeth_predictor(a, b, c));
            } else {
                return false;
            }
            prow[x] = recon;
        }
    }
    return true;
}

bool png_decode(const u8* data, u32 size, image* out) {
    if (size < PNG_SIG_LEN) {
        return false;
    }
    for (u32 i = 0; i < PNG_SIG_LEN; i = i + 1) {
        if (data[i] != PNG_SIGNATURE[i]) {
            return false;
        }
    }

    u32 width = 0;
    u32 height = 0;
    u8 color_type = 0;
    bool have_ihdr = false;
    u32 idat_total = 0;

    // Pass 1: validate every chunk's CRC, parse IHDR, sum IDAT length.
    u32 pos = PNG_SIG_LEN;
    while (pos + 8 <= size) {
        u32 clen = read_u32be(&data[pos]);
        const u8* ctype = &data[pos + 4];
        const u8* cdata = &data[pos + 8];
        if (pos + 8 + (u64) clen + 4 > size) {
            return false;
        }
        u32 stored_crc = read_u32be(&data[pos + 8 + clen]);
        if (crc32_of(ctype, 4 + clen) != stored_crc) {
            return false;
        }

        if (chunk_type_is(ctype, 'I', 'H', 'D', 'R')) {
            if (clen != 13) {
                return false;
            }
            width = read_u32be(&cdata[0]);
            height = read_u32be(&cdata[4]);
            u8 bit_depth = cdata[8];
            color_type = cdata[9];
            u8 compression = cdata[10];
            u8 filter_method = cdata[11];
            u8 interlace = cdata[12];
            if (bit_depth != 8) return false;
            if (color_type != 2 && color_type != 6) return false;
            if (compression != 0 || filter_method != 0 || interlace != 0) return false;
            if (width == 0 || height == 0) return false;
            have_ihdr = true;
        } else if (chunk_type_is(ctype, 'I', 'D', 'A', 'T')) {
            idat_total = idat_total + clen;
        } else if (chunk_type_is(ctype, 'I', 'E', 'N', 'D')) {
            break;
        }
        pos = pos + 8 + clen + 4;
    }

    if (!have_ihdr || idat_total < 6) {
        return false;
    }
    u32 bpp = (color_type == 6) ? 4u : 3u;

    // Pass 2: concatenate all IDAT chunk bytes (PNG allows the
    // compressed stream to be split across several IDAT chunks).
    u8* idat = (u8*) kalloc(idat_total);
    if (!idat) {
        return false;
    }
    u32 idat_pos = 0;
    pos = PNG_SIG_LEN;
    while (pos + 8 <= size) {
        u32 clen = read_u32be(&data[pos]);
        const u8* ctype = &data[pos + 4];
        const u8* cdata = &data[pos + 8];
        if (chunk_type_is(ctype, 'I', 'D', 'A', 'T')) {
            for (u32 i = 0; i < clen; i = i + 1) {
                idat[idat_pos + i] = cdata[i];
            }
            idat_pos = idat_pos + clen;
        } else if (chunk_type_is(ctype, 'I', 'E', 'N', 'D')) {
            break;
        }
        pos = pos + 8 + clen + 4;
    }

    // zlib header (RFC1950): CMF/FLG. CM must be 8 (deflate); FDICT must
    // be unset (a preset-dictionary stream never comes out of a plain
    // PNG encoder).
    u8 cmf = idat[0];
    u8 flg = idat[1];
    if ((cmf & 0x0Fu) != 8 || (flg & 0x20u) != 0) {
        kfree(idat);
        return false;
    }

    u32 raw_size = height * (1 + bpp * width);
    u8* raw = (u8*) kalloc(raw_size);
    if (!raw) {
        kfree(idat);
        return false;
    }

    u32 inflated_len = 0;
    bool ok = inflate(&idat[2], idat_total - 2 - 4, raw, raw_size, &inflated_len);
    if (!ok || inflated_len != raw_size) {
        kfree(raw);
        kfree(idat);
        return false;
    }

    u32 stored_adler = read_u32be(&idat[idat_total - 4]);
    u32 computed_adler = adler32_of(raw, raw_size);
    kfree(idat);
    if (stored_adler != computed_adler) {
        kfree(raw);
        return false;
    }

    if (!defilter(raw, width, height, bpp)) {
        kfree(raw);
        return false;
    }

    u32* pixels = (u32*) kalloc(width * height * 4);
    if (!pixels) {
        kfree(raw);
        return false;
    }
    u32 stride = bpp * width;
    for (u32 y = 0; y < height; y = y + 1) {
        const u8* prow = &raw[y * (1 + stride) + 1];
        for (u32 x = 0; x < width; x = x + 1) {
            u8 r = prow[x * bpp + 0];
            u8 g = prow[x * bpp + 1];
            u8 bch = prow[x * bpp + 2];
            u32 color;
            if (bpp == 4 && prow[x * bpp + 3] == 0) {
                color = IMAGE_TRANSPARENT;
            } else {
                color = ((u32) r << 16) | ((u32) g << 8) | (u32) bch;
            }
            pixels[y * width + x] = color;
        }
    }
    kfree(raw);

    out->width = width;
    out->height = height;
    out->pixels = pixels;
    return true;
}
