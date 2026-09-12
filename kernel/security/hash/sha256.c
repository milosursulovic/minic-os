// Hand-written SHA-256 (FIPS 180-4) - see sha256.h's own comment for why
// this is real, hand-implemented code rather than a ported library (the
// no-external-library rule's own stated exception is for well-known
// public constants/algorithms, the same defense kernel/lib/rand.c's own
// xorshift32 comment already makes). No lookup-table speed tricks, no
// cleverness - correctness/readability over speed, matching this
// codebase's own style everywhere else (this kernel only ever hashes
// small executable images, tens of KB at most - performance is a
// complete non-issue here).

#include "sha256.h"

// FIPS 180-4 section 4.2.2 - the first 32 bits of the fractional parts of
// the cube roots of the first 64 primes. Real, published constants.
static const u32 K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static u32 rotr(u32 x, u32 n) {
    return (x >> n) | (x << (32 - n));
}

static void sha256_process_block(sha256_ctx* ctx, const u8* block) {
    u32 w[64];
    int t = 0;
    while (t < 16) {
        w[t] = ((u32) block[t * 4] << 24) | ((u32) block[t * 4 + 1] << 16)
             | ((u32) block[t * 4 + 2] << 8) | ((u32) block[t * 4 + 3]);
        t = t + 1;
    }
    while (t < 64) {
        u32 s0 = rotr(w[t - 15], 7) ^ rotr(w[t - 15], 18) ^ (w[t - 15] >> 3);
        u32 s1 = rotr(w[t - 2], 17) ^ rotr(w[t - 2], 19) ^ (w[t - 2] >> 10);
        w[t] = w[t - 16] + s0 + w[t - 7] + s1;
        t = t + 1;
    }

    u32 a = ctx->state[0];
    u32 b = ctx->state[1];
    u32 c = ctx->state[2];
    u32 d = ctx->state[3];
    u32 e = ctx->state[4];
    u32 f = ctx->state[5];
    u32 g = ctx->state[6];
    u32 h = ctx->state[7];

    t = 0;
    while (t < 64) {
        u32 big_s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        u32 ch = (e & f) ^ ((~e) & g);
        u32 t1 = h + big_s1 + ch + K[t] + w[t];
        u32 big_s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        u32 maj = (a & b) ^ (a & c) ^ (b & c);
        u32 t2 = big_s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
        t = t + 1;
    }

    ctx->state[0] = ctx->state[0] + a;
    ctx->state[1] = ctx->state[1] + b;
    ctx->state[2] = ctx->state[2] + c;
    ctx->state[3] = ctx->state[3] + d;
    ctx->state[4] = ctx->state[4] + e;
    ctx->state[5] = ctx->state[5] + f;
    ctx->state[6] = ctx->state[6] + g;
    ctx->state[7] = ctx->state[7] + h;
}

// FIPS 180-4 section 5.3.3 - the first 32 bits of the fractional parts of
// the square roots of the first 8 primes. Real, published constants.
void sha256_init(sha256_ctx* ctx) {
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
    ctx->bit_count = 0;
    ctx->buffer_len = 0;
}

void sha256_update(sha256_ctx* ctx, const u8* data, u32 len) {
    u32 i = 0;
    while (i < len) {
        ctx->buffer[ctx->buffer_len] = data[i];
        ctx->buffer_len = ctx->buffer_len + 1;
        ctx->bit_count = ctx->bit_count + 8;
        if (ctx->buffer_len == 64) {
            sha256_process_block(ctx, ctx->buffer);
            ctx->buffer_len = 0;
        }
        i = i + 1;
    }
}

// Standard Merkle-Damgard padding: a mandatory 0x80 byte, zero bytes up
// to a 56-byte boundary (leaving exactly 8 bytes free in the final
// block), then the real pre-padding message length as a 64-bit
// big-endian bit count. Reuses sha256_update() itself for the padding
// bytes (simplest correct approach, real message sizes here are tiny) -
// note the message's true bit count is snapshotted BEFORE padding starts,
// since sha256_update() keeps incrementing ctx->bit_count for every byte
// pushed, padding included; only the snapshot is used for the trailing
// length field.
void sha256_final(sha256_ctx* ctx, u8 out[32]) {
    u64 message_bit_count = ctx->bit_count;

    u8 pad_byte = 0x80;
    sha256_update(ctx, &pad_byte, 1);

    u8 zero = 0;
    while (ctx->buffer_len != 56) {
        sha256_update(ctx, &zero, 1);
    }

    u8 length_bytes[8];
    int i = 0;
    while (i < 8) {
        length_bytes[i] = (u8) (message_bit_count >> (56 - i * 8));
        i = i + 1;
    }
    sha256_update(ctx, length_bytes, 8);  // buffer_len goes 56->64, processes the final block

    i = 0;
    while (i < 8) {
        out[i * 4] = (u8) (ctx->state[i] >> 24);
        out[i * 4 + 1] = (u8) (ctx->state[i] >> 16);
        out[i * 4 + 2] = (u8) (ctx->state[i] >> 8);
        out[i * 4 + 3] = (u8) (ctx->state[i]);
        i = i + 1;
    }
}
