// Hand-written SHA-1 (FIPS 180-4) - see sha1.h's own comment. Same
// correctness-over-speed style as sha256.c (no lookup-table tricks) -
// this kernel only ever hashes small buffers (WPA2 key derivation
// inputs, tens of bytes), performance is a non-issue.

#include "sha1.h"

static u32 rotl(u32 x, u32 n) {
    return (x << n) | (x >> (32 - n));
}

static void sha1_process_block(sha1_ctx* ctx, const u8* block) {
    u32 w[80];
    int t = 0;
    while (t < 16) {
        w[t] = ((u32) block[t * 4] << 24) | ((u32) block[t * 4 + 1] << 16)
             | ((u32) block[t * 4 + 2] << 8) | ((u32) block[t * 4 + 3]);
        t = t + 1;
    }
    while (t < 80) {
        w[t] = rotl(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);
        t = t + 1;
    }

    u32 a = ctx->state[0];
    u32 b = ctx->state[1];
    u32 c = ctx->state[2];
    u32 d = ctx->state[3];
    u32 e = ctx->state[4];

    t = 0;
    while (t < 80) {
        u32 f;
        u32 k;
        if (t < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999u;
        } else if (t < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1u;
        } else if (t < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdcu;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6u;
        }
        u32 temp = rotl(a, 5) + f + e + k + w[t];
        e = d;
        d = c;
        c = rotl(b, 30);
        b = a;
        a = temp;
        t = t + 1;
    }

    ctx->state[0] = ctx->state[0] + a;
    ctx->state[1] = ctx->state[1] + b;
    ctx->state[2] = ctx->state[2] + c;
    ctx->state[3] = ctx->state[3] + d;
    ctx->state[4] = ctx->state[4] + e;
}

void sha1_init(sha1_ctx* ctx) {
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xefcdab89u;
    ctx->state[2] = 0x98badcfeu;
    ctx->state[3] = 0x10325476u;
    ctx->state[4] = 0xc3d2e1f0u;
    ctx->bit_count = 0;
    ctx->buffer_len = 0;
}

void sha1_update(sha1_ctx* ctx, const u8* data, u32 len) {
    u32 i = 0;
    while (i < len) {
        ctx->buffer[ctx->buffer_len] = data[i];
        ctx->buffer_len = ctx->buffer_len + 1;
        ctx->bit_count = ctx->bit_count + 8;
        if (ctx->buffer_len == 64) {
            sha1_process_block(ctx, ctx->buffer);
            ctx->buffer_len = 0;
        }
        i = i + 1;
    }
}

void sha1_final(sha1_ctx* ctx, u8 out[20]) {
    u64 message_bit_count = ctx->bit_count;

    u8 pad_byte = 0x80;
    sha1_update(ctx, &pad_byte, 1);

    u8 zero = 0;
    while (ctx->buffer_len != 56) {
        sha1_update(ctx, &zero, 1);
    }

    u8 length_bytes[8];
    int i = 0;
    while (i < 8) {
        length_bytes[i] = (u8) (message_bit_count >> (56 - i * 8));
        i = i + 1;
    }
    sha1_update(ctx, length_bytes, 8);

    i = 0;
    while (i < 5) {
        out[i * 4] = (u8) (ctx->state[i] >> 24);
        out[i * 4 + 1] = (u8) (ctx->state[i] >> 16);
        out[i * 4 + 2] = (u8) (ctx->state[i] >> 8);
        out[i * 4 + 3] = (u8) (ctx->state[i]);
        i = i + 1;
    }
}
