#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

// Own worker task, separate from io_request's - keeps a slow network
// op from stalling a pending file one. is_dns picks ping vs DNS.
#define NET_PING_SLOTS 2

typedef struct {
    bool used;
    bool done;
    bool is_dns;
    u8 target_ip[4];    // ping: the address to ping
    char hostname[64];  // dns: the hostname to resolve
    bool ok;
    u8 resolved_ip[4];  // dns: the resolved address, only meaningful if ok
} net_ping_request;

extern net_ping_request g_net_ping_requests[NET_PING_SLOTS];

int alloc_net_ping_request(u8* target_ip);
int alloc_net_dns_request(const char* hostname);
void free_net_ping_request(int slot_index);
void net_worker_entry(void);

#pragma GCC visibility pop
