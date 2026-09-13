#pragma once
#include "../../../types.h"
#include "../tcp/tcp.h"
#include "../../security/bignum/bignum.h"
#include "../../security/hash/sha256.h"

#pragma GCC visibility push(hidden)

// Hand-written TLS 1.2 client - TLS_RSA_WITH_AES_128_CBC_SHA256 only, no
// extensions/resumption/renegotiation. See kernel/net/tls/tls.c's own
// top-of-file comment for the real, stated scope limits (trust-on-first-use
// certificate acceptance, non-cryptographic RNG) - this proves the
// hand-written protocol/crypto stack is wire-correct and interoperable
// with a real, independent TLS implementation, not production security.

typedef struct {
    tcp_conn_t tcp;

    u8 client_random[32];
    u8 server_random[32];

    u8 master_secret[48];
    u8 client_write_mac_key[32];
    u8 server_write_mac_key[32];
    u8 client_write_key[16];
    u8 server_write_key[16];
    u8 client_round_keys[176];
    u8 server_round_keys[176];

    u64 client_seq_num;
    u64 server_seq_num;

    sha256_ctx transcript;  // running hash of every Handshake-type message, both directions
} tls_conn_t;

// Full handshake: ClientHello -> ServerHello/Certificate/ServerHelloDone ->
// ClientKeyExchange -> ChangeCipherSpec+Finished -> verify server's
// ChangeCipherSpec+Finished. Returns true only if the server's Finished
// verify_data matches - a real, decisive cryptographic proof every prior
// step (RSA encrypt, PRF/key derivation, AES-CBC, HMAC, transcript
// hashing) was correct and interoperated with the real peer.
bool tls_connect(u8* ip, u16 port, tls_conn_t* conn);

bool tls_send(tls_conn_t* conn, const u8* data, u16 len);
bool tls_receive(tls_conn_t* conn, u8* buf, u16 max_len, u64 timeout_ticks, u16* len_out);
void tls_close(tls_conn_t* conn);

#pragma GCC visibility pop
