#pragma once

#include "../../../types.h"

// Hand-written SHA-256 (FIPS 180-4) - the published, standard round
// constants/algorithm, not someone else's code, same category as
// kernel/inflate/inflate.c's RFC1951 tables or kernel/lib/rand.c's
// xorshift32 constants. Streaming API (init/update/final) so a caller
// never needs a second full copy of whatever it's hashing. Deliberately
// portable (types.h only, no kernel globals/freestanding-only
// constructs) - this same .c file compiles both into the kernel and into
// tools/sign_exec.c's plain host-gcc build, so signer and verifier are
// always the exact same code, never two hand-copies that could drift.

#pragma GCC visibility push(hidden)

typedef struct {
    u32 state[8];
    u64 bit_count;
    u8 buffer[64];
    u32 buffer_len;
} sha256_ctx;

void sha256_init(sha256_ctx* ctx);
void sha256_update(sha256_ctx* ctx, const u8* data, u32 len);
void sha256_final(sha256_ctx* ctx, u8 out[32]);

#pragma GCC visibility pop
