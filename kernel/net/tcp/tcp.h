#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Single-shot client fetch: handshake, send request, read reply, best-effort
// FIN close. Returns true only if the handshake completed and at least one
// byte was received; *response_len_out is set either way.
bool tcp_fetch(u8* target_ip, u16 target_port, const char* request, u16 request_len,
               u8* response_out, u32 max_response_len, u32* response_len_out);

#define TCP_CONNECTION_SLOTS 4

typedef struct {
    bool used;
    u16 local_port;
    u8 remote_ip[4];
    u16 remote_port;
    // Server-side only: a real accepted connection is long-lived across
    // several send/receive calls, unlike the client fetch path's single
    // handshake-send-receive-close - it needs to remember the peer's MAC
    // (no ARP round-trip needed to reply - just echo the incoming
    // frame's own source MAC back) and sequence state between calls.
    u8 remote_mac[6];
    u32 my_seq;
    u32 peer_seq;
} tcp_connection;

extern tcp_connection g_tcp_connections[TCP_CONNECTION_SLOTS];

// Real interactive (multi-round-trip) client stream, unlike tcp_fetch()'s
// single-shot request/response - needed by anything that has to hold a
// live connection open across several separate sends/receives (a TLS
// handshake's many messages, a future HTTP keep-alive connection). Thin
// wrappers around this file's own existing tcp_conn_open/tcp_send_reliable/
// tcp_send_segment/tcp_wait_segment/tcp_conn_close - no logic duplicated,
// same handshake/close sequence tcp_fetch_conn() already uses.
#define TCP_STREAM_PENDING_BUF_LEN 1500

typedef struct {
    int pool_slot;
    u8 gateway_mac[6];
    u8 target_ip[4];
    u16 target_port;
    u16 local_port;
    u32 my_seq;
    u32 peer_seq;
    bool peer_finished;
    // A send can have the peer's reply fused onto its own ACK (a real,
    // fast-replying peer commonly does this) - tcp_stream_send() stashes
    // that payload here instead of discarding it, so the next
    // tcp_stream_receive() call drains it before waiting on the wire.
    u8 pending_buf[TCP_STREAM_PENDING_BUF_LEN];
    u16 pending_len;
} tcp_conn_t;

bool tcp_stream_open(u8* ip, u16 port, tcp_conn_t* conn);
bool tcp_stream_send(tcp_conn_t* conn, const u8* data, u16 len);
// Tick-bounded poll for the next chunk of incoming bytes (drains any
// already-pending fused payload first, with no wait). Returns true with
// *len_out > 0 on data, false on timeout or once the peer has sent FIN
// with nothing left pending.
bool tcp_stream_receive(tcp_conn_t* conn, u8* buf, u16 max_len, u64 timeout_ticks, u16* len_out);
void tcp_stream_close(tcp_conn_t* conn);

// TEMPORARY test hook - see kernel/net/tcp/tcp.c's own comment. Set
// nonzero right before a tcp_fetch()/tcp_send_reliable() call to
// deliberately simulate one lost packet and prove the retry path fires
// for real.
extern u32 g_tcp_debug_drop_count;

// Real server side: kernel/net/tcp/tcp.c's tcp_send_segment()/
// tcp_wait_segment() (already generic - plain destination MAC + target
// ip/port + seq/ack/flags, nothing client-specific baked in) are reused
// as-is; only waiting for the FIRST SYN from an unknown remote is new.
#define TCP_LISTENER_SLOTS 2

typedef struct {
    bool used;
    u16 port;
} tcp_listener;

extern tcp_listener g_tcp_listeners[TCP_LISTENER_SLOTS];

int tcp_listen(u16 port);
// Blocks (polls) up to timeout_ticks for a real client connecting to
// listener_slot's port, completes the server-side handshake (SYN-ACK,
// wait for the final ACK), returns an ESTABLISHED tcp_connection slot -
// or -1 on timeout/failure.
int tcp_accept(int listener_slot, u64 timeout_ticks, u8* remote_ip_out, u16* remote_port_out);
u32 tcp_server_send(int conn_slot, const u8* data, u16 len);
// Returns the real byte count received (0 on timeout/no data - never blocks
// past timeout_ticks).
u32 tcp_server_receive(int conn_slot, u8* buf, u32 max_len, u64 timeout_ticks);
// Real FIN/ACK exchange (mirrors tcp_fetch_conn's own close sequence),
// then frees the slot.
void tcp_server_close(int conn_slot);

#pragma GCC visibility pop
