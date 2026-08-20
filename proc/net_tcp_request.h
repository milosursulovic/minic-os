#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

// One atomic async tcp_fetch(), own worker/pool - a fetch runs far
// longer than a ping/DNS round trip, so sharing would starve them.
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
