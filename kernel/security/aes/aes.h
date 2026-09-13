#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Hand-written AES-128 (Rijndael), FIPS-197 - block encrypt/decrypt plus a
// CBC-mode wrapper with PKCS#7 padding. Used by kernel/net/tls/tls.c's
// TLS_RSA_WITH_AES_128_CBC_SHA256 cipher suite - no other AES key size is
// needed anywhere in this kernel yet.

#define AES128_ROUND_KEY_BYTES 176  // 11 round keys * 16 bytes

void aes128_key_expand(const u8 key[16], u8 round_keys[AES128_ROUND_KEY_BYTES]);
void aes128_encrypt_block(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 in[16], u8 out[16]);
void aes128_decrypt_block(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 in[16], u8 out[16]);

// CBC mode. `in_len` must be a multiple of 16 for the raw (non-padding)
// variants. The `_pkcs7` variants add/remove PKCS#7 padding themselves;
// `out` must have room for `in_len` rounded up to the next 16-byte
// boundary (padding always adds 1-16 bytes, never zero).
void aes128_cbc_encrypt(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 iv[16],
                         const u8* in, int in_len, u8* out);
void aes128_cbc_decrypt(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 iv[16],
                         const u8* in, int in_len, u8* out);

// TLS 1.2 CBC record padding (RFC 5246 6.2.3.2) - NOT the same as standard
// PKCS#7, despite looking almost identical: TLS's trailing padding_length
// byte counts as one of the added bytes, so block-aligned input's minimum
// valid padding_length is block_size-1 (15), not a full extra block (16)
// the way real PKCS#7 requires. Confirmed empirically against a real
// openssl TLS 1.2 session (padding_length=0x0f observed for a 48-byte
// content+MAC, exactly matching this formula, not plain PKCS#7 - an
// earlier, incorrect PKCS#7-based implementation caused every real
// handshake to fail server-side MAC verification despite every other
// primitive - RSA, PRF/key derivation, HMAC - independently verifying
// correct).
int aes128_cbc_encrypt_tls(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 iv[16],
                            const u8* in, int in_len, u8* out, int out_capacity);
int aes128_cbc_decrypt_tls(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 iv[16],
                            const u8* in, int in_len, u8* out);

#pragma GCC visibility pop
