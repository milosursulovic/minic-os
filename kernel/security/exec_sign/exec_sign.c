// See exec_sign.h's own comment for the header format and this module's
// scope.

#include "exec_sign.h"
#include "signing_key.h"
#include "../hash/hmac_sha256.h"

bool exec_sign_verify(const u8* signed_image, u32 signed_len,
                       const u8** payload_out, u32* payload_len_out) {
    if (signed_len < sizeof(exec_sign_header)) {
        return false;
    }
    const exec_sign_header* header = (const exec_sign_header*) signed_image;
    if (header->magic != EXEC_SIGN_MAGIC) {
        return false;
    }
    if ((u64) sizeof(exec_sign_header) + (u64) header->payload_len != (u64) signed_len) {
        return false;
    }
    const u8* payload = signed_image + sizeof(exec_sign_header);
    u8 computed_hmac[EXEC_SIGN_HMAC_LEN];
    hmac_sha256(EXEC_SIGNING_KEY, EXEC_SIGNING_KEY_LEN, payload, header->payload_len, computed_hmac);
    u32 i = 0;
    while (i < EXEC_SIGN_HMAC_LEN) {
        if (computed_hmac[i] != header->hmac[i]) {
            return false;
        }
        i = i + 1;
    }
    *payload_out = payload;
    *payload_len_out = header->payload_len;
    return true;
}

void exec_sign_produce(const u8* payload, u32 payload_len, exec_sign_header* header_out) {
    header_out->magic = EXEC_SIGN_MAGIC;
    header_out->payload_len = payload_len;
    hmac_sha256(EXEC_SIGNING_KEY, EXEC_SIGNING_KEY_LEN, payload, payload_len, header_out->hmac);
}
