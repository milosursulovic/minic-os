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
