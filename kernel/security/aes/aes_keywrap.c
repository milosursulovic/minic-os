// Hand-written RFC 3394 AES Key Unwrap - see aes_keywrap.h.

#include "aes_keywrap.h"

static const u8 DEFAULT_IV[8] = {0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6, 0xA6};

static void xor_t(u8 a[8], u64 t) {
    int i = 0;
    while (i < 8) {
        a[i] = a[i] ^ (u8) (t >> (56 - i * 8));
        i = i + 1;
    }
}

bool aes_key_unwrap(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8* wrapped,
                     u32 wrapped_len, u8* out) {
    if (wrapped_len < 16 || (wrapped_len % 8) != 0) {
        return false;
    }
    u32 n = (wrapped_len / 8) - 1;

    u8 a[8];
    int i = 0;
    while (i < 8) {
        a[i] = wrapped[i];
        i = i + 1;
    }

    // R[1..n] stored directly into `out[0 .. n*8)` (0-indexed as R[i-1]).
    i = 0;
    while (i < (int) (n * 8)) {
        out[i] = wrapped[8 + i];
        i = i + 1;
    }

    int j = 5;
    while (j >= 0) {
        u32 idx = n;
        while (idx >= 1) {
            u64 t = (u64) (n * (u32) j + idx);
            xor_t(a, t);

            u8 block[16];
            int k = 0;
            while (k < 8) {
                block[k] = a[k];
                block[8 + k] = out[(idx - 1) * 8 + k];
                k = k + 1;
            }
            u8 decrypted[16];
            aes128_decrypt_block(round_keys, block, decrypted);
            k = 0;
            while (k < 8) {
                a[k] = decrypted[k];
                out[(idx - 1) * 8 + k] = decrypted[8 + k];
                k = k + 1;
            }
            idx = idx - 1;
        }
        j = j - 1;
    }

    i = 0;
    while (i < 8) {
        if (a[i] != DEFAULT_IV[i]) {
            return false;
        }
        i = i + 1;
    }
    return true;
}
