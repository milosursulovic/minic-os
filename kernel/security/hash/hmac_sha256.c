// Hand-written HMAC-SHA256 (RFC 2104) - see hmac_sha256.h's own comment.

#include "hmac_sha256.h"
#include "sha256.h"

#define SHA256_BLOCK_SIZE 64
#define SHA256_DIGEST_SIZE 32

void hmac_sha256(const u8* key, u32 key_len, const u8* data, u32 data_len, u8 out[32]) {
    u8 key_block[SHA256_BLOCK_SIZE];
    u32 i = 0;

    if (key_len > SHA256_BLOCK_SIZE) {
        // RFC 2104: keys longer than the block size are hashed down first.
        sha256_ctx key_ctx;
        sha256_init(&key_ctx);
        sha256_update(&key_ctx, key, key_len);
        u8 hashed_key[SHA256_DIGEST_SIZE];
        sha256_final(&key_ctx, hashed_key);
        while (i < SHA256_DIGEST_SIZE) {
            key_block[i] = hashed_key[i];
            i = i + 1;
        }
    } else {
        while (i < key_len) {
            key_block[i] = key[i];
            i = i + 1;
        }
    }
    while (i < SHA256_BLOCK_SIZE) {
        key_block[i] = 0;
        i = i + 1;
    }

    u8 ipad_key[SHA256_BLOCK_SIZE];
    u8 opad_key[SHA256_BLOCK_SIZE];
    i = 0;
    while (i < SHA256_BLOCK_SIZE) {
        ipad_key[i] = key_block[i] ^ 0x36;
        opad_key[i] = key_block[i] ^ 0x5c;
        i = i + 1;
    }

    u8 inner_digest[SHA256_DIGEST_SIZE];
    sha256_ctx ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, ipad_key, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, data, data_len);
    sha256_final(&ctx, inner_digest);

    sha256_init(&ctx);
    sha256_update(&ctx, opad_key, SHA256_BLOCK_SIZE);
    sha256_update(&ctx, inner_digest, SHA256_DIGEST_SIZE);
    sha256_final(&ctx, out);
}
