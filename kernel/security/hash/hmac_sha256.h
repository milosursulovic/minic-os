#pragma once

#include "../../../types.h"

// Hand-written HMAC (RFC 2104) over sha256.h's SHA-256 - the standard,
// published construction, not someone else's code (same defense as
// sha256.h's own comment). Portable (types.h + sha256.h only) - shared
// verbatim between the kernel build and tools/sign_exec.c's host build.

#pragma GCC visibility push(hidden)

void hmac_sha256(const u8* key, u32 key_len, const u8* data, u32 data_len, u8 out[32]);

#pragma GCC visibility pop
