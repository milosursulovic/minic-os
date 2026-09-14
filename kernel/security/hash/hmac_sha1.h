#pragma once

#include "../../../types.h"

// Hand-written HMAC (RFC 2104) over sha1.h's SHA-1 - mirrors
// hmac_sha256.h's own structure exactly, same construction, different
// underlying hash. Needed by kernel/net/rtw89/wpa2.c for the real
// WPA2-PSK PRF (key derivation) and EAPOL-Key MIC (both spec-mandated
// HMAC-SHA1 for a CCMP/AES-based network, not this project's choice).

#pragma GCC visibility push(hidden)

void hmac_sha1(const u8* key, u32 key_len, const u8* data, u32 data_len, u8 out[20]);

#pragma GCC visibility pop
