#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

// Async ICMP ping, the first ring3-facing network capability this
// kernel has - a separate small pool + dedicated worker task from
// io_request's, so one slow ping (icmp_ping()'s own internal timeout
// can run up to 2000 ticks) never starves a pending file request.
#define NET_PING_SLOTS 2

typedef struct {
    bool used;
    bool done;
    u8 target_ip[4];
    bool ok;
} net_ping_request;

extern net_ping_request g_net_ping_requests[NET_PING_SLOTS];

int alloc_net_ping_request(u8* target_ip);
void free_net_ping_request(int slot_index);
void net_worker_entry(void);

#pragma GCC visibility pop
