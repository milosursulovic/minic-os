// Hand-written AES-128 (Rijndael, FIPS-197) - see aes.h's own comment for
// scope. S-box/inverse S-box/Rcon are the standard published FIPS-197
// constant tables (same "well-known public constant" exception already
// used for kernel/security/hash/sha256.c's K[64]).

#include "aes.h"

#define AES_ROUNDS 10
#define AES_PKCS7_MAX_LEN 4096

static const u8 SBOX[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
};

static const u8 INV_SBOX[256] = {
    0x52,0x09,0x6a,0xd5,0x30,0x36,0xa5,0x38,0xbf,0x40,0xa3,0x9e,0x81,0xf3,0xd7,0xfb,
    0x7c,0xe3,0x39,0x82,0x9b,0x2f,0xff,0x87,0x34,0x8e,0x43,0x44,0xc4,0xde,0xe9,0xcb,
    0x54,0x7b,0x94,0x32,0xa6,0xc2,0x23,0x3d,0xee,0x4c,0x95,0x0b,0x42,0xfa,0xc3,0x4e,
    0x08,0x2e,0xa1,0x66,0x28,0xd9,0x24,0xb2,0x76,0x5b,0xa2,0x49,0x6d,0x8b,0xd1,0x25,
    0x72,0xf8,0xf6,0x64,0x86,0x68,0x98,0x16,0xd4,0xa4,0x5c,0xcc,0x5d,0x65,0xb6,0x92,
    0x6c,0x70,0x48,0x50,0xfd,0xed,0xb9,0xda,0x5e,0x15,0x46,0x57,0xa7,0x8d,0x9d,0x84,
    0x90,0xd8,0xab,0x00,0x8c,0xbc,0xd3,0x0a,0xf7,0xe4,0x58,0x05,0xb8,0xb3,0x45,0x06,
    0xd0,0x2c,0x1e,0x8f,0xca,0x3f,0x0f,0x02,0xc1,0xaf,0xbd,0x03,0x01,0x13,0x8a,0x6b,
    0x3a,0x91,0x11,0x41,0x4f,0x67,0xdc,0xea,0x97,0xf2,0xcf,0xce,0xf0,0xb4,0xe6,0x73,
    0x96,0xac,0x74,0x22,0xe7,0xad,0x35,0x85,0xe2,0xf9,0x37,0xe8,0x1c,0x75,0xdf,0x6e,
    0x47,0xf1,0x1a,0x71,0x1d,0x29,0xc5,0x89,0x6f,0xb7,0x62,0x0e,0xaa,0x18,0xbe,0x1b,
    0xfc,0x56,0x3e,0x4b,0xc6,0xd2,0x79,0x20,0x9a,0xdb,0xc0,0xfe,0x78,0xcd,0x5a,0xf4,
    0x1f,0xdd,0xa8,0x33,0x88,0x07,0xc7,0x31,0xb1,0x12,0x10,0x59,0x27,0x80,0xec,0x5f,
    0x60,0x51,0x7f,0xa9,0x19,0xb5,0x4a,0x0d,0x2d,0xe5,0x7a,0x9f,0x93,0xc9,0x9c,0xef,
    0xa0,0xe0,0x3b,0x4d,0xae,0x2a,0xf5,0xb0,0xc8,0xeb,0xbb,0x3c,0x83,0x53,0x99,0x61,
    0x17,0x2b,0x04,0x7e,0xba,0x77,0xd6,0x26,0xe1,0x69,0x14,0x63,0x55,0x21,0x0c,0x7d
};

static const u8 RCON[11] = { 0x00,0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36 };

static u8 gf_mul(u8 a, u8 b) {
    u8 result = 0;
    u8 aa = a;
    u8 bb = b;
    int i = 0;
    while (i < 8) {
        if (bb & 1) {
            result = result ^ aa;
        }
        u8 hi = (u8) (aa & 0x80);
        aa = (u8) (aa << 1);
        if (hi) {
            aa = aa ^ 0x1B;
        }
        bb = (u8) (bb >> 1);
        i = i + 1;
    }
    return result;
}

static void sub_bytes(u8 state[16]) {
    int i = 0;
    while (i < 16) {
        state[i] = SBOX[state[i]];
        i = i + 1;
    }
}

static void inv_sub_bytes(u8 state[16]) {
    int i = 0;
    while (i < 16) {
        state[i] = INV_SBOX[state[i]];
        i = i + 1;
    }
}

// state[r + 4*c] addressing (FIPS-197's column-major state, matching how
// the 16 input bytes fill the state a column at a time).
static void shift_rows(u8 state[16]) {
    u8 tmp[16];
    int r = 0;
    while (r < 4) {
        int c = 0;
        while (c < 4) {
            tmp[r + 4 * c] = state[r + 4 * ((c + r) % 4)];
            c = c + 1;
        }
        r = r + 1;
    }
    int i = 0;
    while (i < 16) {
        state[i] = tmp[i];
        i = i + 1;
    }
}

static void inv_shift_rows(u8 state[16]) {
    u8 tmp[16];
    int r = 0;
    while (r < 4) {
        int c = 0;
        while (c < 4) {
            tmp[r + 4 * c] = state[r + 4 * ((c - r + 4) % 4)];
            c = c + 1;
        }
        r = r + 1;
    }
    int i = 0;
    while (i < 16) {
        state[i] = tmp[i];
        i = i + 1;
    }
}

static void mix_columns(u8 state[16]) {
    int c = 0;
    while (c < 4) {
        u8 a0 = state[4 * c + 0];
        u8 a1 = state[4 * c + 1];
        u8 a2 = state[4 * c + 2];
        u8 a3 = state[4 * c + 3];
        state[4 * c + 0] = (u8) (gf_mul(a0, 2) ^ gf_mul(a1, 3) ^ a2 ^ a3);
        state[4 * c + 1] = (u8) (a0 ^ gf_mul(a1, 2) ^ gf_mul(a2, 3) ^ a3);
        state[4 * c + 2] = (u8) (a0 ^ a1 ^ gf_mul(a2, 2) ^ gf_mul(a3, 3));
        state[4 * c + 3] = (u8) (gf_mul(a0, 3) ^ a1 ^ a2 ^ gf_mul(a3, 2));
        c = c + 1;
    }
}

static void inv_mix_columns(u8 state[16]) {
    int c = 0;
    while (c < 4) {
        u8 a0 = state[4 * c + 0];
        u8 a1 = state[4 * c + 1];
        u8 a2 = state[4 * c + 2];
        u8 a3 = state[4 * c + 3];
        state[4 * c + 0] = (u8) (gf_mul(a0, 14) ^ gf_mul(a1, 11) ^ gf_mul(a2, 13) ^ gf_mul(a3, 9));
        state[4 * c + 1] = (u8) (gf_mul(a0, 9) ^ gf_mul(a1, 14) ^ gf_mul(a2, 11) ^ gf_mul(a3, 13));
        state[4 * c + 2] = (u8) (gf_mul(a0, 13) ^ gf_mul(a1, 9) ^ gf_mul(a2, 14) ^ gf_mul(a3, 11));
        state[4 * c + 3] = (u8) (gf_mul(a0, 11) ^ gf_mul(a1, 13) ^ gf_mul(a2, 9) ^ gf_mul(a3, 14));
        c = c + 1;
    }
}

static void add_round_key(u8 state[16], const u8* round_key) {
    int i = 0;
    while (i < 16) {
        state[i] = (u8) (state[i] ^ round_key[i]);
        i = i + 1;
    }
}

void aes128_key_expand(const u8 key[16], u8 round_keys[AES128_ROUND_KEY_BYTES]) {
    u8 w[44][4];
    int i = 0;
    while (i < 4) {
        w[i][0] = key[4 * i + 0];
        w[i][1] = key[4 * i + 1];
        w[i][2] = key[4 * i + 2];
        w[i][3] = key[4 * i + 3];
        i = i + 1;
    }
    i = 4;
    while (i < 44) {
        u8 temp[4];
        temp[0] = w[i - 1][0];
        temp[1] = w[i - 1][1];
        temp[2] = w[i - 1][2];
        temp[3] = w[i - 1][3];
        if (i % 4 == 0) {
            u8 rotated[4];
            rotated[0] = temp[1];
            rotated[1] = temp[2];
            rotated[2] = temp[3];
            rotated[3] = temp[0];
            temp[0] = (u8) (SBOX[rotated[0]] ^ RCON[i / 4]);
            temp[1] = SBOX[rotated[1]];
            temp[2] = SBOX[rotated[2]];
            temp[3] = SBOX[rotated[3]];
        }
        w[i][0] = (u8) (w[i - 4][0] ^ temp[0]);
        w[i][1] = (u8) (w[i - 4][1] ^ temp[1]);
        w[i][2] = (u8) (w[i - 4][2] ^ temp[2]);
        w[i][3] = (u8) (w[i - 4][3] ^ temp[3]);
        i = i + 1;
    }
    i = 0;
    while (i < 44) {
        round_keys[4 * i + 0] = w[i][0];
        round_keys[4 * i + 1] = w[i][1];
        round_keys[4 * i + 2] = w[i][2];
        round_keys[4 * i + 3] = w[i][3];
        i = i + 1;
    }
}

void aes128_encrypt_block(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 in[16], u8 out[16]) {
    u8 state[16];
    int i = 0;
    while (i < 16) {
        state[i] = in[i];
        i = i + 1;
    }
    add_round_key(state, &round_keys[0]);

    int round = 1;
    while (round < AES_ROUNDS) {
        sub_bytes(state);
        shift_rows(state);
        mix_columns(state);
        add_round_key(state, &round_keys[16 * round]);
        round = round + 1;
    }

    sub_bytes(state);
    shift_rows(state);
    add_round_key(state, &round_keys[16 * AES_ROUNDS]);

    i = 0;
    while (i < 16) {
        out[i] = state[i];
        i = i + 1;
    }
}

void aes128_decrypt_block(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 in[16], u8 out[16]) {
    u8 state[16];
    int i = 0;
    while (i < 16) {
        state[i] = in[i];
        i = i + 1;
    }
    add_round_key(state, &round_keys[16 * AES_ROUNDS]);

    int round = AES_ROUNDS - 1;
    while (round >= 1) {
        inv_shift_rows(state);
        inv_sub_bytes(state);
        add_round_key(state, &round_keys[16 * round]);
        inv_mix_columns(state);
        round = round - 1;
    }

    inv_shift_rows(state);
    inv_sub_bytes(state);
    add_round_key(state, &round_keys[0]);

    i = 0;
    while (i < 16) {
        out[i] = state[i];
        i = i + 1;
    }
}

void aes128_cbc_encrypt(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 iv[16],
                         const u8* in, int in_len, u8* out) {
    u8 prev[16];
    int i = 0;
    while (i < 16) {
        prev[i] = iv[i];
        i = i + 1;
    }
    int blocks = in_len / 16;
    int b = 0;
    while (b < blocks) {
        u8 block[16];
        i = 0;
        while (i < 16) {
            block[i] = (u8) (in[16 * b + i] ^ prev[i]);
            i = i + 1;
        }
        aes128_encrypt_block(round_keys, block, &out[16 * b]);
        i = 0;
        while (i < 16) {
            prev[i] = out[16 * b + i];
            i = i + 1;
        }
        b = b + 1;
    }
}

void aes128_cbc_decrypt(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 iv[16],
                         const u8* in, int in_len, u8* out) {
    u8 prev[16];
    int i = 0;
    while (i < 16) {
        prev[i] = iv[i];
        i = i + 1;
    }
    int blocks = in_len / 16;
    int b = 0;
    while (b < blocks) {
        u8 decrypted[16];
        aes128_decrypt_block(round_keys, &in[16 * b], decrypted);
        i = 0;
        while (i < 16) {
            out[16 * b + i] = (u8) (decrypted[i] ^ prev[i]);
            i = i + 1;
        }
        i = 0;
        while (i < 16) {
            prev[i] = in[16 * b + i];
            i = i + 1;
        }
        b = b + 1;
    }
}

int aes128_cbc_encrypt_tls(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 iv[16],
                            const u8* in, int in_len, u8* out, int out_capacity) {
    int pad_len = (16 - ((in_len + 1) % 16)) % 16;  // 0..15
    int total = in_len + pad_len + 1;
    if (total > out_capacity || total > AES_PKCS7_MAX_LEN) {
        return -1;
    }
    u8 padded[AES_PKCS7_MAX_LEN];
    int i = 0;
    while (i < in_len) {
        padded[i] = in[i];
        i = i + 1;
    }
    while (i < total - 1) {
        padded[i] = (u8) pad_len;
        i = i + 1;
    }
    padded[total - 1] = (u8) pad_len;
    aes128_cbc_encrypt(round_keys, iv, padded, total, out);
    return total;
}

int aes128_cbc_decrypt_tls(const u8 round_keys[AES128_ROUND_KEY_BYTES], const u8 iv[16],
                            const u8* in, int in_len, u8* out) {
    if (in_len <= 0 || (in_len % 16) != 0 || in_len > AES_PKCS7_MAX_LEN) {
        return -1;
    }
    u8 decrypted[AES_PKCS7_MAX_LEN];
    aes128_cbc_decrypt(round_keys, iv, in, in_len, decrypted);

    u8 pad_len = decrypted[in_len - 1];
    if (pad_len > 15 || (int) pad_len + 1 > in_len) {
        return -1;
    }
    int i = in_len - 1 - pad_len;
    while (i < in_len - 1) {
        if (decrypted[i] != pad_len) {
            return -1;
        }
        i = i + 1;
    }
    int plain_len = in_len - pad_len - 1;
    i = 0;
    while (i < plain_len) {
        out[i] = decrypted[i];
        i = i + 1;
    }
    return plain_len;
}
