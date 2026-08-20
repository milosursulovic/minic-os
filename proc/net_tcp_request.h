#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

// Async TCP fetch - a real 3-way handshake, one request segment, a
// receive loop, and a best-effort close, all as ONE atomic async
// operation (matching tcp_fetch()'s own existing all-in-one shape)
// rather than exposing a raw connect/send/recv/close socket API to
// ring3. Its own separate pool + worker task from net_ping_request's:
// a full fetch can run several times longer than a single ping/DNS
// round trip (the handshake and the receive loop each have their own
// ~2000-3000 tick budget internally), so sharing a worker would let a
// slow fetch stall a pending ping/DNS request.
#define TCP_REQUEST_SLOTS 2
#define TCP_REQUEST_PAYLOAD_SIZE 256
#define TCP_REQUEST_RESPONSE_SIZE 512

typedef struct {
    bool used;
    bool done;
    u8 target_ip[4];
    u16 target_port;
    char payload[TCP_REQUEST_PAYLOAD_SIZE];
    u16 payload_len;
    u8 response[TCP_REQUEST_RESPONSE_SIZE];
    u32 response_len;
    bool ok;
} net_tcp_request;

extern net_tcp_request g_net_tcp_requests[TCP_REQUEST_SLOTS];

int alloc_net_tcp_request(u8* target_ip, u16 target_port, const char* payload, u16 payload_len);
void free_net_tcp_request(int slot_index);
void tcp_worker_entry(void);

#pragma GCC visibility pop
