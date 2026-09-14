#pragma once

#include "../../../types.h"
#include "aes.h"

// Hand-written NIST AES Key Wrap, RFC 3394 - unwrap direction only.
// Real, standard construction (not designed here) - kernel/net/rtw89/
// wpa2.c needs it to unwrap the AP's real GTK (group key) out of
// EAPOL-Key message 3's key-data field, per 802.11i's own mandated use
// of RFC 3394 AES-KW for a CCMP/AES-based network's key-data
// encryption. Reuses the existing aes128_decrypt_block primitive
// (kernel/security/aes/aes.c) - no new block-cipher code.

#pragma GCC visibility push(hidden)

// `wrapped_len` must be a multiple of 8 and at least 16 (RFC 3394's
// minimum: one 8-byte IV block + at least one 8-byte plaintext block).
// Writes `wrapped_len - 8` bytes to `out`. Returns false if the
// standard integrity check (the recovered IV must equal the fixed
// 0xA6A6A6A6A6A6A6A6 default IV) fails - a real, decisive signal the
// unwrap key (KEK) or ciphertext was wrong, not silently accepted.
bool aes_key_unwrap(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8* wrapped,
                     u32 wrapped_len, u8* out);

#pragma GCC visibility pop
