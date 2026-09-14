// Hand-written HMAC-SHA1 (RFC 2104) - see hmac_sha1.h. Mirrors
// hmac_sha256.c structurally (same block size, 64 bytes for both hashes).

#include "hmac_sha1.h"
#include "sha1.h"

#define SHA1_BLOCK_SIZE 64
#define SHA1_DIGEST_SIZE 20

void hmac_sha1(const u8* key, u32 key_len, const u8* data, u32 data_len, u8 out[20]) {
    u8 key_block[SHA1_BLOCK_SIZE];
    u32 i = 0;

    if (key_len > SHA1_BLOCK_SIZE) {
        sha1_ctx key_ctx;
        sha1_init(&key_ctx);
        sha1_update(&key_ctx, key, key_len);
        u8 hashed_key[SHA1_DIGEST_SIZE];
        sha1_final(&key_ctx, hashed_key);
        while (i < SHA1_DIGEST_SIZE) {
            key_block[i] = hashed_key[i];
            i = i + 1;
        }
    } else {
        while (i < key_len) {
            key_block[i] = key[i];
            i = i + 1;
        }
    }
    while (i < SHA1_BLOCK_SIZE) {
        key_block[i] = 0;
        i = i + 1;
    }

    u8 ipad_key[SHA1_BLOCK_SIZE];
    u8 opad_key[SHA1_BLOCK_SIZE];
    i = 0;
    while (i < SHA1_BLOCK_SIZE) {
        ipad_key[i] = key_block[i] ^ 0x36;
        opad_key[i] = key_block[i] ^ 0x5c;
        i = i + 1;
    }

    u8 inner_digest[SHA1_DIGEST_SIZE];
    sha1_ctx ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, ipad_key, SHA1_BLOCK_SIZE);
    sha1_update(&ctx, data, data_len);
    sha1_final(&ctx, inner_digest);

    sha1_init(&ctx);
    sha1_update(&ctx, opad_key, SHA1_BLOCK_SIZE);
    sha1_update(&ctx, inner_digest, SHA1_DIGEST_SIZE);
    sha1_final(&ctx, out);
}
