// Hand-written PBKDF2-HMAC-SHA1 (RFC 2898) - see pbkdf2_sha1.h.

#include "pbkdf2_sha1.h"
#include "hmac_sha1.h"

#define SHA1_DIGEST_SIZE 20

// F(P, S, c, i) = U1 ^ U2 ^ ... ^ Uc, U1 = HMAC(P, S || INT(i)), Uj = HMAC(P, U(j-1)).
static void pbkdf2_block(const u8* passphrase, u32 passphrase_len, const u8* salt, u32 salt_len,
                          u32 iterations, u32 block_index, u8 out[SHA1_DIGEST_SIZE]) {
    u8 salt_and_index[64 + 4];  // real salts here (an SSID) are <=32 bytes
    u32 i = 0;
    while (i < salt_len) {
        salt_and_index[i] = salt[i];
        i = i + 1;
    }
    salt_and_index[salt_len + 0] = (u8) (block_index >> 24);
    salt_and_index[salt_len + 1] = (u8) (block_index >> 16);
    salt_and_index[salt_len + 2] = (u8) (block_index >> 8);
    salt_and_index[salt_len + 3] = (u8) block_index;

    u8 u[SHA1_DIGEST_SIZE];
    hmac_sha1(passphrase, passphrase_len, salt_and_index, salt_len + 4, u);

    i = 0;
    while (i < SHA1_DIGEST_SIZE) {
        out[i] = u[i];
        i = i + 1;
    }

    u32 iter = 1;
    while (iter < iterations) {
        hmac_sha1(passphrase, passphrase_len, u, SHA1_DIGEST_SIZE, u);
        i = 0;
        while (i < SHA1_DIGEST_SIZE) {
            out[i] = out[i] ^ u[i];
            i = i + 1;
        }
        iter = iter + 1;
    }
}

void pbkdf2_sha1(const u8* passphrase, u32 passphrase_len, const u8* salt, u32 salt_len,
                  u32 iterations, u8* out, u32 out_len) {
    u32 block_index = 1;
    u32 produced = 0;
    while (produced < out_len) {
        u8 block[SHA1_DIGEST_SIZE];
        pbkdf2_block(passphrase, passphrase_len, salt, salt_len, iterations, block_index, block);
        u32 n = out_len - produced;
        if (n > SHA1_DIGEST_SIZE) {
            n = SHA1_DIGEST_SIZE;
        }
        u32 i = 0;
        while (i < n) {
            out[produced + i] = block[i];
            i = i + 1;
        }
        produced = produced + n;
        block_index = block_index + 1;
    }
}
