#pragma once

#include "../../../types.h"

// Hand-written PBKDF2 (RFC 2898) over hmac_sha1.h - the standard,
// published key-derivation construction. Needed for real WPA2-PSK: a
// network's actual encryption key material (the PMK) is
// PBKDF2-SHA1(passphrase, ssid, 4096 iterations, 256 bits) - a fixed
// standard (RFC 2898 / IEEE 802.11i Annex H), not a design choice this
// project made.

#pragma GCC visibility push(hidden)

// `out_len` in bytes (WPA2-PSK always uses 32 - 256 bits - but this
// stays general, matching PBKDF2's own real definition rather than
// hardcoding the one caller's constant here).
void pbkdf2_sha1(const u8* passphrase, u32 passphrase_len, const u8* salt, u32 salt_len,
                  u32 iterations, u8* out, u32 out_len);

#pragma GCC visibility pop
