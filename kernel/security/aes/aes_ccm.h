#pragma once

#include "../../../types.h"
#include "aes.h"

// Hand-written AES-CCM (NIST SP 800-38C), fixed to the exact parameters
// 802.11i's CCMP always uses: M=8 (8-byte MIC), L=2 (2-byte length
// field, since a real 802.11 MSDU never exceeds 2^16 bytes) - not a
// general-purpose CCM implementation, the one real, fixed instantiation
// CCMP needs. Reuses the existing aes128_encrypt_block primitive
// (kernel/security/aes/aes.c) for both the CTR-mode keystream and the
// CBC-MAC - no new block-cipher code, same discipline as
// aes_keywrap.h/aes128_cbc_*.
//
// The caller (kernel/net/rtw89/dot11.c) builds the real 13-byte CCMP
// nonce (priority || A2 || PN, per 802.11i 12.5.3.3.4) and the real AAD
// (masked FC || A1 || A2 || A3 || masked SC || masked QC, per
// 12.5.3.3.3) from the actual 802.11 header fields - this file stays a
// generic CCM engine, not 802.11-specific.

#pragma GCC visibility push(hidden)

#define AES_CCM_MIC_LEN 8
#define AES_CCM_NONCE_LEN 13

// `out` gets `len` bytes of ciphertext; `mic_out` gets the real 8-byte
// MIC (already CTR-encrypted per spec, ready to append to the frame).
void aes_ccm_encrypt(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 nonce[AES_CCM_NONCE_LEN],
                      const u8* aad, u32 aad_len, const u8* plaintext, u32 len,
                      u8* out, u8 mic_out[AES_CCM_MIC_LEN]);

// Decrypts into `out` and independently recomputes the MIC, comparing
// it against `mic_in` - returns false on any mismatch (a real,
// decisive tamper/wrong-key signal, matching this project's existing
// TLS MAC-verification discipline) without asserting anything about
// `out`'s trustworthiness in that case.
bool aes_ccm_decrypt(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 nonce[AES_CCM_NONCE_LEN],
                      const u8* aad, u32 aad_len, const u8* ciphertext, u32 len,
                      const u8 mic_in[AES_CCM_MIC_LEN], u8* out);

#pragma GCC visibility pop
