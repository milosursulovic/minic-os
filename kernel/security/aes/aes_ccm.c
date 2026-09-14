// Hand-written AES-CCM, fixed to CCMP's M=8/L=2 parameters (NIST SP
// 800-38C) - see aes_ccm.h.

#include "aes_ccm.h"

static void build_b0(const u8 nonce[AES_CCM_NONCE_LEN], u32 msg_len, bool has_aad, u8 out[16]) {
    out[0] = (u8) ((has_aad ? 0x40 : 0) | (3 << 3) | 1);  // Adata | (M-2)/2=3 | L-1=1
    int i = 0;
    while (i < AES_CCM_NONCE_LEN) {
        out[1 + i] = nonce[i];
        i = i + 1;
    }
    out[14] = (u8) (msg_len >> 8);
    out[15] = (u8) msg_len;
}

static void build_a(const u8 nonce[AES_CCM_NONCE_LEN], u32 counter, u8 out[16]) {
    out[0] = 0x01;  // L-1=1, no Adata/M fields in a counter block
    int i = 0;
    while (i < AES_CCM_NONCE_LEN) {
        out[1 + i] = nonce[i];
        i = i + 1;
    }
    out[14] = (u8) (counter >> 8);
    out[15] = (u8) counter;
}

static void xor_block(u8* dst, const u8* src, u32 n) {
    u32 i = 0;
    while (i < n) {
        dst[i] = dst[i] ^ src[i];
        i = i + 1;
    }
}

// CBC-MAC over B0, the length-prefixed+padded AAD, then the
// length-padded message (the real plaintext, per CCM's own definition -
// callers must pass plaintext even when verifying a decrypt, never
// ciphertext).
static void cbc_mac(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 nonce[AES_CCM_NONCE_LEN],
                     const u8* aad, u32 aad_len, const u8* msg, u32 msg_len, u8 mac_out[16]) {
    u8 x[16];
    u8 b0[16];
    build_b0(nonce, msg_len, aad_len > 0, b0);
    aes128_encrypt_block(round_keys, b0, x);

    if (aad_len > 0) {
        u8 block[16];
        u32 i = 0;
        while (i < 16) {
            block[i] = 0;
            i = i + 1;
        }
        block[0] = (u8) (aad_len >> 8);
        block[1] = (u8) aad_len;
        u32 filled = 2;
        u32 consumed = 0;
        while (consumed < aad_len) {
            while (filled < 16 && consumed < aad_len) {
                block[filled] = aad[consumed];
                filled = filled + 1;
                consumed = consumed + 1;
            }
            xor_block(x, block, 16);
            u8 enc[16];
            aes128_encrypt_block(round_keys, x, enc);
            i = 0;
            while (i < 16) {
                x[i] = enc[i];
                i = i + 1;
            }
            filled = 0;
            i = 0;
            while (i < 16) {
                block[i] = 0;
                i = i + 1;
            }
        }
    }

    u32 consumed = 0;
    while (consumed < msg_len) {
        u8 block[16];
        u32 i = 0;
        while (i < 16) {
            block[i] = 0;
            i = i + 1;
        }
        u32 chunk = msg_len - consumed;
        if (chunk > 16) {
            chunk = 16;
        }
        i = 0;
        while (i < chunk) {
            block[i] = msg[consumed + i];
            i = i + 1;
        }
        xor_block(x, block, 16);
        u8 enc[16];
        aes128_encrypt_block(round_keys, x, enc);
        i = 0;
        while (i < 16) {
            x[i] = enc[i];
            i = i + 1;
        }
        consumed = consumed + chunk;
    }
    // msg_len==0 (never real for CCMP data, but handled honestly) still
    // needs the B0-only MAC already computed above - nothing further.

    int i = 0;
    while (i < 16) {
        mac_out[i] = x[i];
        i = i + 1;
    }
}

static void ctr_crypt(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 nonce[AES_CCM_NONCE_LEN],
                       const u8* in, u32 len, u8* out) {
    u32 consumed = 0;
    u32 counter = 1;
    while (consumed < len) {
        u8 a[16];
        build_a(nonce, counter, a);
        u8 s[16];
        aes128_encrypt_block(round_keys, a, s);
        u32 chunk = len - consumed;
        if (chunk > 16) {
            chunk = 16;
        }
        u32 i = 0;
        while (i < chunk) {
            out[consumed + i] = in[consumed + i] ^ s[i];
            i = i + 1;
        }
        consumed = consumed + chunk;
        counter = counter + 1;
    }
}

static void mic_mask(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 nonce[AES_CCM_NONCE_LEN],
                      u8 mask_out[AES_CCM_MIC_LEN]) {
    u8 a0[16];
    build_a(nonce, 0, a0);
    u8 s0[16];
    aes128_encrypt_block(round_keys, a0, s0);
    int i = 0;
    while (i < AES_CCM_MIC_LEN) {
        mask_out[i] = s0[i];
        i = i + 1;
    }
}

void aes_ccm_encrypt(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 nonce[AES_CCM_NONCE_LEN],
                      const u8* aad, u32 aad_len, const u8* plaintext, u32 len,
                      u8* out, u8 mic_out[AES_CCM_MIC_LEN]) {
    u8 mac[16];
    cbc_mac(round_keys, nonce, aad, aad_len, plaintext, len, mac);

    ctr_crypt(round_keys, nonce, plaintext, len, out);

    u8 mask[AES_CCM_MIC_LEN];
    mic_mask(round_keys, nonce, mask);
    int i = 0;
    while (i < AES_CCM_MIC_LEN) {
        mic_out[i] = mac[i] ^ mask[i];
        i = i + 1;
    }
}

bool aes_ccm_decrypt(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 nonce[AES_CCM_NONCE_LEN],
                      const u8* aad, u32 aad_len, const u8* ciphertext, u32 len,
                      const u8 mic_in[AES_CCM_MIC_LEN], u8* out) {
    ctr_crypt(round_keys, nonce, ciphertext, len, out);

    u8 mac[16];
    cbc_mac(round_keys, nonce, aad, aad_len, out, len, mac);

    u8 mask[AES_CCM_MIC_LEN];
    mic_mask(round_keys, nonce, mask);

    int i = 0;
    while (i < AES_CCM_MIC_LEN) {
        if ((u8) (mac[i] ^ mask[i]) != mic_in[i]) {
            return false;
        }
        i = i + 1;
    }
    return true;
}
