// Hand-written TLS 1.2 client (RFC 5246), TLS_RSA_WITH_AES_128_CBC_SHA256
// only - no extensions, no session resumption, no renegotiation, client
// role only (matches this whole networking-completion arc's existing
// "client path only, server explicitly out of scope" precedent from
// retransmission/IPv6/icmp6).
//
// Real, stated scope limits (deliberate, not oversights):
//  - Certificate trust is trust-on-first-use - the leaf certificate's RSA
//    public key is used as-is, with NO signature verification and NO
//    chain-of-trust walk (same spirit as exec_sign.h's single embedded
//    key, or DHCP's "no lease renewal": an honest, stated boundary, not a
//    hidden gap).
//  - Randomness (client_random, the pre-master-secret's random bytes,
//    PKCS#1 v1.5 padding bytes, each record's explicit CBC IV) comes from
//    kernel/lib/rand.c's existing xorshift32 - NOT cryptographically
//    secure. This item's goal is proving the hand-written protocol/crypto
//    stack (bignum/RSA, AES-128-CBC, HMAC-SHA256, the TLS record/handshake
//    state machine) is wire-correct and interoperates with a real,
//    independent TLS implementation (verified against openssl s_server),
//    not achieving production-grade security.
//  - The RSA operation is always *encryption* with the server's small
//    public exponent (e.g. 65537, 17 bits) - never a private-key
//    operation, which is why kernel/security/bignum/bignum.c's schoolbook
//    modexp (no Montgomery optimization) is fast enough here.

#include "tls.h"
#include "../../security/aes/aes.h"
#include "../../security/x509/x509.h"
#include "../../security/hash/hmac_sha256.h"
#include "../../lib/rand.h"
#include "../../isr/isr.h"
#include "../../sched/task.h"

#define TLS_CONTENT_CHANGE_CIPHER_SPEC 20
#define TLS_CONTENT_ALERT 21
#define TLS_CONTENT_HANDSHAKE 22
#define TLS_CONTENT_APPLICATION_DATA 23

#define TLS_HANDSHAKE_CLIENT_HELLO 1
#define TLS_HANDSHAKE_SERVER_HELLO 2
#define TLS_HANDSHAKE_CERTIFICATE 11
#define TLS_HANDSHAKE_SERVER_HELLO_DONE 14
#define TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE 16
#define TLS_HANDSHAKE_FINISHED 20

#define TLS_CIPHER_SUITE_HI 0x00
#define TLS_CIPHER_SUITE_LO 0x3C  // TLS_RSA_WITH_AES_128_CBC_SHA256

#define TLS_TIMEOUT_TICKS 3000

// Bounds every record payload / handshake buffer this client handles - a
// real self-signed 2048-bit RSA test certificate is a few hundred bytes,
// and this item's own application-data proof is a small HTTP
// request/response, not an arbitrary-size download (same "bounded, stated
// capacity" precedent as tcp_fetch()'s own max_response_len).
#define TLS_MAX_RECORD_LEN 1200
#define TLS_RECORD_BUF_LEN (5 + TLS_MAX_RECORD_LEN + 96)

static bool bytes_equal(const u8* a, const u8* b, int len) {
    int i = 0;
    while (i < len) {
        if (a[i] != b[i]) {
            return false;
        }
        i = i + 1;
    }
    return true;
}

static u8 random_byte(void) {
    static u32 cache = 0;
    static int cache_bytes_left = 0;
    if (cache_bytes_left == 0) {
        cache = rand_next();
        cache_bytes_left = 4;
    }
    u8 b = (u8) (cache & 0xFF);
    cache = cache >> 8;
    cache_bytes_left = cache_bytes_left - 1;
    return b;
}

static void fill_random(u8* out, int len) {
    int i = 0;
    while (i < len) {
        out[i] = random_byte();
        i = i + 1;
    }
}

static void sha256_ctx_copy(sha256_ctx* dst, const sha256_ctx* src) {
    int i = 0;
    while (i < 8) {
        dst->state[i] = src->state[i];
        i = i + 1;
    }
    dst->bit_count = src->bit_count;
    i = 0;
    while (i < 64) {
        dst->buffer[i] = src->buffer[i];
        i = i + 1;
    }
    dst->buffer_len = src->buffer_len;
}

// RFC 5246 5.: P_hash(secret,seed) = HMAC(A(1)+seed) || HMAC(A(2)+seed) || ...,
// A(0) = seed, A(i) = HMAC(secret, A(i-1)). `label` is concatenated in
// front of `seed_data` to form the actual PRF seed.
static void tls_prf(const u8* secret, int secret_len, const char* label,
                     const u8* seed_data, int seed_data_len, u8* out, int out_len) {
    u8 seed[128];
    int seed_len = 0;
    int i = 0;
    while (label[i] != '\0') {
        seed[seed_len] = (u8) label[i];
        seed_len = seed_len + 1;
        i = i + 1;
    }
    i = 0;
    while (i < seed_data_len) {
        seed[seed_len] = seed_data[i];
        seed_len = seed_len + 1;
        i = i + 1;
    }

    u8 a[32];
    hmac_sha256(secret, (u32) secret_len, seed, (u32) seed_len, a);

    int produced = 0;
    while (produced < out_len) {
        u8 input[32 + 128];
        int input_len = 0;
        int j = 0;
        while (j < 32) {
            input[input_len] = a[j];
            input_len = input_len + 1;
            j = j + 1;
        }
        j = 0;
        while (j < seed_len) {
            input[input_len] = seed[j];
            input_len = input_len + 1;
            j = j + 1;
        }
        u8 hmac_out[32];
        hmac_sha256(secret, (u32) secret_len, input, (u32) input_len, hmac_out);

        int copy_len = 32;
        if (produced + copy_len > out_len) {
            copy_len = out_len - produced;
        }
        j = 0;
        while (j < copy_len) {
            out[produced + j] = hmac_out[j];
            j = j + 1;
        }
        produced = produced + copy_len;

        u8 next_a[32];
        hmac_sha256(secret, (u32) secret_len, a, 32, next_a);
        j = 0;
        while (j < 32) {
            a[j] = next_a[j];
            j = j + 1;
        }
    }
}

static bool tcp_read_exact(tcp_conn_t* tcp, u8* buf, u16 len, u64 timeout_ticks) {
    u16 got = 0;
    u64 deadline = g_tick_count + timeout_ticks;
    while (got < len) {
        u64 remaining = (deadline > g_tick_count) ? (deadline - g_tick_count) : 0;
        if (remaining == 0) {
            return false;
        }
        u16 chunk;
        if (!tcp_stream_receive(tcp, &buf[got], (u16) (len - got), remaining, &chunk)) {
            return false;
        }
        got = (u16) (got + chunk);
    }
    return true;
}

static bool tls_read_record(tls_conn_t* conn, u8* type_out, u8* payload_buf, u16 max_payload_len, u16* payload_len_out) {
    u8 header[5];
    if (!tcp_read_exact(&conn->tcp, header, 5, TLS_TIMEOUT_TICKS)) {
        return false;
    }
    u16 length = (u16) ((((u16) header[3]) << 8) | (u16) header[4]);
    if (length > max_payload_len) {
        return false;
    }
    if (!tcp_read_exact(&conn->tcp, payload_buf, length, TLS_TIMEOUT_TICKS)) {
        return false;
    }
    *type_out = header[0];
    *payload_len_out = length;
    return true;
}

// Frames+sends `data` as one record. Used for genuinely plaintext handshake
// messages (ClientHello, ClientKeyExchange) AND for already-encrypted
// fragments (post-ChangeCipherSpec) - framing itself never changes.
static bool tls_send_record(tls_conn_t* conn, u8 content_type, const u8* data, u16 len) {
    u8 record[TLS_RECORD_BUF_LEN];
    record[0] = content_type;
    record[1] = 0x03;
    record[2] = 0x03;
    record[3] = (u8) (len >> 8);
    record[4] = (u8) (len & 0xFF);
    int i = 0;
    while (i < (int) len) {
        record[5 + i] = data[i];
        i = i + 1;
    }
    return tcp_stream_send(&conn->tcp, record, (u16) (5 + len));
}

static void generate_iv(u8 iv[16]) {
    fill_random(iv, 16);
}

// RFC 5246 6.2.3.1 - MAC-then-encrypt: MAC = HMAC(mac_key, seq_num(8) ||
// type(1) || version(2) || length(2) || content).
static void compute_record_mac(const u8 mac_key[32], u64 seq_num, u8 content_type,
                                u16 content_len, const u8* content, u8 mac_out[32]) {
    u8 mac_input[13 + TLS_MAX_RECORD_LEN];
    int p = 0;
    int i = 7;
    while (i >= 0) {
        mac_input[p] = (u8) ((seq_num >> (i * 8)) & 0xFF);
        p = p + 1;
        i = i - 1;
    }
    mac_input[p] = content_type;
    p = p + 1;
    mac_input[p] = 0x03;
    p = p + 1;
    mac_input[p] = 0x03;
    p = p + 1;
    mac_input[p] = (u8) (content_len >> 8);
    p = p + 1;
    mac_input[p] = (u8) (content_len & 0xFF);
    p = p + 1;
    i = 0;
    while (i < (int) content_len) {
        mac_input[p + i] = content[i];
        i = i + 1;
    }
    p = p + content_len;
    hmac_sha256(mac_key, 32, mac_input, (u32) p, mac_out);
}

static bool send_encrypted_record(tls_conn_t* conn, u8 content_type, const u8* content, u16 content_len) {
    u8 mac[32];
    compute_record_mac(conn->client_write_mac_key, conn->client_seq_num, content_type, content_len, content, mac);

    u8 plain[TLS_MAX_RECORD_LEN + 32];
    int i = 0;
    while (i < (int) content_len) {
        plain[i] = content[i];
        i = i + 1;
    }
    i = 0;
    while (i < 32) {
        plain[content_len + i] = mac[i];
        i = i + 1;
    }
    int plain_len = content_len + 32;

    u8 iv[16];
    generate_iv(iv);

    u8 ciphertext[TLS_MAX_RECORD_LEN + 64];
    int cipher_len = aes128_cbc_encrypt_tls(conn->client_round_keys, iv, plain, plain_len,
                                             ciphertext, (int) sizeof(ciphertext));
    if (cipher_len < 0) {
        return false;
    }

    u8 fragment[16 + TLS_MAX_RECORD_LEN + 64];
    i = 0;
    while (i < 16) {
        fragment[i] = iv[i];
        i = i + 1;
    }
    i = 0;
    while (i < cipher_len) {
        fragment[16 + i] = ciphertext[i];
        i = i + 1;
    }

    conn->client_seq_num = conn->client_seq_num + 1;
    return tls_send_record(conn, content_type, fragment, (u16) (16 + cipher_len));
}

static bool receive_encrypted_record(tls_conn_t* conn, u8* type_out, u8* content_out,
                                      u16 max_content_len, u16* content_len_out) {
    u8 raw[16 + TLS_MAX_RECORD_LEN + 64];
    u16 raw_len;
    if (!tls_read_record(conn, type_out, raw, (u16) sizeof(raw), &raw_len)) {
        return false;
    }
    if (raw_len < 32) {  // need at least a 16-byte IV + one 16-byte AES block
        return false;
    }
    u8* iv = &raw[0];
    u8* ciphertext = &raw[16];
    int ciphertext_len = raw_len - 16;

    u8 decrypted[TLS_MAX_RECORD_LEN + 64];
    int plain_len = aes128_cbc_decrypt_tls(conn->server_round_keys, iv, ciphertext, ciphertext_len, decrypted);
    if (plain_len < 32) {
        return false;  // must at least contain the 32-byte HMAC-SHA256 MAC
    }
    int content_len = plain_len - 32;
    const u8* mac_received = &decrypted[content_len];

    u8 mac_expected[32];
    compute_record_mac(conn->server_write_mac_key, conn->server_seq_num, *type_out,
                        (u16) content_len, decrypted, mac_expected);
    if (!bytes_equal(mac_received, mac_expected, 32)) {
        return false;
    }
    conn->server_seq_num = conn->server_seq_num + 1;

    if (content_len > (int) max_content_len) {
        content_len = (int) max_content_len;
    }
    int i = 0;
    while (i < content_len) {
        content_out[i] = decrypted[i];
        i = i + 1;
    }
    *content_len_out = (u16) content_len;
    return true;
}

static bool send_client_hello(tls_conn_t* conn) {
    fill_random(conn->client_random, 32);

    u8 body[64];
    int p = 0;
    body[p] = 0x03; p = p + 1;
    body[p] = 0x03; p = p + 1;
    int i = 0;
    while (i < 32) {
        body[p] = conn->client_random[i];
        p = p + 1;
        i = i + 1;
    }
    body[p] = 0x00; p = p + 1;  // session_id length = 0
    body[p] = 0x00; p = p + 1;
    body[p] = 0x02; p = p + 1;  // cipher_suites length = 2 (one suite)
    body[p] = TLS_CIPHER_SUITE_HI; p = p + 1;
    body[p] = TLS_CIPHER_SUITE_LO; p = p + 1;
    body[p] = 0x01; p = p + 1;  // compression_methods length = 1
    body[p] = 0x00; p = p + 1;  // null compression

    // signature_algorithms extension (RFC 5246 7.4.1.4.1, type 0x000D) -
    // without it, a real server has no way to know the client accepts its
    // certificate's own signing algorithm and (confirmed empirically
    // against openssl 3.5.5's s_server) refuses the handshake outright
    // ("no suitable signature algorithm") even for a static-RSA cipher
    // suite that needs no server signature of its own. One entry,
    // matching this test certificate's real sha256WithRSAEncryption
    // signature (SignatureAndHashAlgorithm: hash=sha256(4), sig=rsa(1)).
    body[p] = 0x00; p = p + 1;  // extensions length (2 bytes) = 8
    body[p] = 0x08; p = p + 1;
    body[p] = 0x00; p = p + 1;  // extension type = signature_algorithms (0x000D)
    body[p] = 0x0D; p = p + 1;
    body[p] = 0x00; p = p + 1;  // extension_data length = 4
    body[p] = 0x04; p = p + 1;
    body[p] = 0x00; p = p + 1;  // supported_signature_algorithms length = 2
    body[p] = 0x02; p = p + 1;
    body[p] = 0x04; p = p + 1;  // hash = sha256
    body[p] = 0x01; p = p + 1;  // signature = rsa

    u16 body_len = (u16) p;

    u8 msg[4 + 64];
    msg[0] = TLS_HANDSHAKE_CLIENT_HELLO;
    msg[1] = (u8) (body_len >> 16);
    msg[2] = (u8) (body_len >> 8);
    msg[3] = (u8) (body_len & 0xFF);
    i = 0;
    while (i < body_len) {
        msg[4 + i] = body[i];
        i = i + 1;
    }
    u16 msg_len = (u16) (4 + body_len);

    sha256_update(&conn->transcript, msg, msg_len);
    return tls_send_record(conn, TLS_CONTENT_HANDSHAKE, msg, msg_len);
}

// Reads Handshake-type records until ServerHelloDone, extracting
// server_random and (from the leaf certificate) the RSA public key.
static bool receive_server_flight(tls_conn_t* conn, bignum_t* server_modulus, u32* server_exponent) {
    bool got_done = false;
    bool got_cert = false;
    u8 record_buf[TLS_MAX_RECORD_LEN];

    while (!got_done) {
        u8 type;
        u16 len;
        if (!tls_read_record(conn, &type, record_buf, TLS_MAX_RECORD_LEN, &len)) {
            return false;
        }
        if (type != TLS_CONTENT_HANDSHAKE) {
            return false;
        }
        int offset = 0;
        while (offset < (int) len) {
            if (offset + 4 > (int) len) {
                return false;
            }
            u8 hs_type = record_buf[offset];
            int hs_len = (((int) record_buf[offset + 1]) << 16)
                | (((int) record_buf[offset + 2]) << 8)
                | (int) record_buf[offset + 3];
            int hs_total = 4 + hs_len;
            if (offset + hs_total > (int) len) {
                return false;
            }
            sha256_update(&conn->transcript, &record_buf[offset], (u32) hs_total);

            if (hs_type == TLS_HANDSHAKE_SERVER_HELLO) {
                int body_off = offset + 4;
                int i = 0;
                while (i < 32) {
                    conn->server_random[i] = record_buf[body_off + 2 + i];
                    i = i + 1;
                }
            } else if (hs_type == TLS_HANDSHAKE_CERTIFICATE) {
                int body_off = offset + 4;
                int first_cert_len = (((int) record_buf[body_off + 3]) << 16)
                    | (((int) record_buf[body_off + 4]) << 8)
                    | (int) record_buf[body_off + 5];
                const u8* cert_der = &record_buf[body_off + 6];
                if (!x509_extract_rsa_pubkey(cert_der, first_cert_len, server_modulus, server_exponent)) {
                    return false;
                }
                got_cert = true;
            } else if (hs_type == TLS_HANDSHAKE_SERVER_HELLO_DONE) {
                got_done = true;
            }
            offset = offset + hs_total;
        }
    }
    return got_cert;
}

static bool send_client_key_exchange(tls_conn_t* conn, const bignum_t* server_modulus,
                                      u32 server_exponent, const u8 pre_master_secret[48]) {
    int modulus_len = (bignum_bit_length(server_modulus) + 7) / 8;
    if (modulus_len < 3 + 48 + 8 || modulus_len > 512) {
        return false;  // too small for PKCS#1 v1.5 padding, or beyond bignum's 4096-bit capacity
    }

    // PKCS#1 v1.5: 0x00 0x02 <nonzero random padding> 0x00 <pre_master_secret>
    u8 block[512];
    block[0] = 0x00;
    block[1] = 0x02;
    int ps_len = modulus_len - 3 - 48;
    int i = 0;
    while (i < ps_len) {
        u8 b;
        do {
            b = random_byte();
        } while (b == 0);
        block[2 + i] = b;
        i = i + 1;
    }
    block[2 + ps_len] = 0x00;
    i = 0;
    while (i < 48) {
        block[2 + ps_len + 1 + i] = pre_master_secret[i];
        i = i + 1;
    }

    bignum_t base, exponent_bn, result;
    bignum_from_bytes_be(&base, block, modulus_len);
    bignum_set_u32(&exponent_bn, server_exponent);
    bignum_modexp(&result, &base, &exponent_bn, server_modulus);

    u8 ciphertext[512];
    bignum_to_bytes_be(&result, ciphertext, modulus_len);

    u8 body[2 + 512];
    body[0] = (u8) (modulus_len >> 8);
    body[1] = (u8) (modulus_len & 0xFF);
    i = 0;
    while (i < modulus_len) {
        body[2 + i] = ciphertext[i];
        i = i + 1;
    }
    u16 body_len = (u16) (2 + modulus_len);

    u8 msg[4 + 2 + 512];
    msg[0] = TLS_HANDSHAKE_CLIENT_KEY_EXCHANGE;
    msg[1] = (u8) (body_len >> 16);
    msg[2] = (u8) (body_len >> 8);
    msg[3] = (u8) (body_len & 0xFF);
    i = 0;
    while (i < body_len) {
        msg[4 + i] = body[i];
        i = i + 1;
    }
    u16 msg_len = (u16) (4 + body_len);

    sha256_update(&conn->transcript, msg, msg_len);
    return tls_send_record(conn, TLS_CONTENT_HANDSHAKE, msg, msg_len);
}

static void derive_keys(tls_conn_t* conn, const u8 pre_master_secret[48]) {
    u8 seed_master[64];
    int i = 0;
    while (i < 32) {
        seed_master[i] = conn->client_random[i];
        i = i + 1;
    }
    while (i < 64) {
        seed_master[i] = conn->server_random[i - 32];
        i = i + 1;
    }
    tls_prf(pre_master_secret, 48, "master secret", seed_master, 64, conn->master_secret, 48);

    u8 seed_keys[64];
    i = 0;
    while (i < 32) {
        seed_keys[i] = conn->server_random[i];
        i = i + 1;
    }
    while (i < 64) {
        seed_keys[i] = conn->client_random[i - 32];
        i = i + 1;
    }
    u8 key_block[96];
    tls_prf(conn->master_secret, 48, "key expansion", seed_keys, 64, key_block, 96);

    i = 0;
    while (i < 32) {
        conn->client_write_mac_key[i] = key_block[i];
        i = i + 1;
    }
    i = 0;
    while (i < 32) {
        conn->server_write_mac_key[i] = key_block[32 + i];
        i = i + 1;
    }
    i = 0;
    while (i < 16) {
        conn->client_write_key[i] = key_block[64 + i];
        i = i + 1;
    }
    i = 0;
    while (i < 16) {
        conn->server_write_key[i] = key_block[80 + i];
        i = i + 1;
    }

    aes128_key_expand(conn->client_write_key, conn->client_round_keys);
    aes128_key_expand(conn->server_write_key, conn->server_round_keys);
}

static bool send_change_cipher_spec(tls_conn_t* conn) {
    u8 payload = 0x01;
    return tls_send_record(conn, TLS_CONTENT_CHANGE_CIPHER_SPEC, &payload, 1);
}

static bool send_finished(tls_conn_t* conn) {
    sha256_ctx snapshot;
    sha256_ctx_copy(&snapshot, &conn->transcript);
    u8 transcript_hash[32];
    sha256_final(&snapshot, transcript_hash);

    u8 verify_data[12];
    tls_prf(conn->master_secret, 48, "client finished", transcript_hash, 32, verify_data, 12);

    u8 msg[16];
    msg[0] = TLS_HANDSHAKE_FINISHED;
    msg[1] = 0x00;
    msg[2] = 0x00;
    msg[3] = 0x0C;
    int i = 0;
    while (i < 12) {
        msg[4 + i] = verify_data[i];
        i = i + 1;
    }

    sha256_update(&conn->transcript, msg, 16);
    return send_encrypted_record(conn, TLS_CONTENT_HANDSHAKE, msg, 16);
}

static bool receive_server_change_cipher_spec_and_finished(tls_conn_t* conn) {
    u8 type;
    u8 payload[8];
    u16 len;
    if (!tls_read_record(conn, &type, payload, sizeof(payload), &len)) {
        return false;
    }
    if (type != TLS_CONTENT_CHANGE_CIPHER_SPEC || len != 1 || payload[0] != 0x01) {
        return false;
    }

    // Expected server verify_data uses the transcript up to and including
    // the CLIENT's own Finished message (already fed in by send_finished()).
    sha256_ctx snapshot;
    sha256_ctx_copy(&snapshot, &conn->transcript);
    u8 transcript_hash[32];
    sha256_final(&snapshot, transcript_hash);
    u8 expected_verify_data[12];
    tls_prf(conn->master_secret, 48, "server finished", transcript_hash, 32, expected_verify_data, 12);

    u8 content[64];
    u16 content_len;
    if (!receive_encrypted_record(conn, &type, content, sizeof(content), &content_len)) {
        return false;
    }
    if (type != TLS_CONTENT_HANDSHAKE || content_len != 16 || content[0] != TLS_HANDSHAKE_FINISHED) {
        return false;
    }
    // This is the decisive correctness proof: it only matches if RSA
    // encrypt, PRF/key derivation, AES-CBC, HMAC, and transcript hashing
    // were ALL bit-exact correct and interoperated with the real peer.
    return bytes_equal(&content[4], expected_verify_data, 12);
}

bool tls_connect(u8* ip, u16 port, tls_conn_t* conn) {
    if (!tcp_stream_open(ip, port, &conn->tcp)) {
        return false;
    }
    sha256_init(&conn->transcript);
    conn->client_seq_num = 0;
    conn->server_seq_num = 0;

    if (!send_client_hello(conn)) {
        tcp_stream_close(&conn->tcp);
        return false;
    }

    bignum_t server_modulus;
    u32 server_exponent;
    if (!receive_server_flight(conn, &server_modulus, &server_exponent)) {
        tcp_stream_close(&conn->tcp);
        return false;
    }

    u8 pre_master_secret[48];
    pre_master_secret[0] = 0x03;
    pre_master_secret[1] = 0x03;
    fill_random(&pre_master_secret[2], 46);

    if (!send_client_key_exchange(conn, &server_modulus, server_exponent, pre_master_secret)) {
        tcp_stream_close(&conn->tcp);
        return false;
    }

    derive_keys(conn, pre_master_secret);

    if (!send_change_cipher_spec(conn)) {
        tcp_stream_close(&conn->tcp);
        return false;
    }
    if (!send_finished(conn)) {
        tcp_stream_close(&conn->tcp);
        return false;
    }
    if (!receive_server_change_cipher_spec_and_finished(conn)) {
        tcp_stream_close(&conn->tcp);
        return false;
    }

    return true;
}

bool tls_send(tls_conn_t* conn, const u8* data, u16 len) {
    if (len > TLS_MAX_RECORD_LEN) {
        return false;
    }
    return send_encrypted_record(conn, TLS_CONTENT_APPLICATION_DATA, data, len);
}

bool tls_receive(tls_conn_t* conn, u8* buf, u16 max_len, u64 timeout_ticks, u16* len_out) {
    u64 deadline = g_tick_count + timeout_ticks;
    while (g_tick_count < deadline) {
        u8 type;
        u16 content_len;
        if (receive_encrypted_record(conn, &type, buf, max_len, &content_len)) {
            if (type == TLS_CONTENT_APPLICATION_DATA) {
                *len_out = content_len;
                return true;
            }
            if (type == TLS_CONTENT_ALERT) {
                *len_out = 0;
                return false;
            }
            // anything else (stray handshake/CCS) - keep waiting
        } else {
            *len_out = 0;
            return false;
        }
    }
    *len_out = 0;
    return false;
}

void tls_close(tls_conn_t* conn) {
    tcp_stream_close(&conn->tcp);
}
