#pragma once

#include "../../../types.h"

// Hand-written SHA-1 (FIPS 180-4) - same category/discipline as
// sha256.h's own comment (published, standard algorithm/constants, not
// someone else's code). Needed for kernel/net/rtw89/wpa2.c's real
// WPA2-PSK key derivation (PBKDF2-SHA1 + the PRF/MIC, both HMAC-SHA1
// based per 802.11i - a fixed, standards-mandated hash choice for
// WPA2-PSK/CCMP, not a preference). Same streaming init/update/final
// shape as sha256.h, deliberately portable (types.h only).

#pragma GCC visibility push(hidden)

typedef struct {
    u32 state[5];
    u64 bit_count;
    u8 buffer[64];
    u32 buffer_len;
} sha1_ctx;

void sha1_init(sha1_ctx* ctx);
void sha1_update(sha1_ctx* ctx, const u8* data, u32 len);
void sha1_final(sha1_ctx* ctx, u8 out[20]);

#pragma GCC visibility pop
