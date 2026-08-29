// Backs syscalls 20-23 (async ping/DNS, issue+wait).

#include "net_request.h"
#include "../../net/icmp.h"
#include "../../net/dns.h"
#include "../../kernel/sched/task.h"

net_ping_request g_net_ping_requests[NET_PING_SLOTS];

static const u16 PING_IDENTIFIER = 0x5150;  // "PQ" - fixed, distinguishes this worker's own pings

static int alloc_net_request_slot(void) {
    int i = 0;
    while (i < NET_PING_SLOTS) {
        if (!g_net_ping_requests[i].used) {
            g_net_ping_requests[i].used = true;
            g_net_ping_requests[i].done = false;
            g_net_ping_requests[i].ok = false;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

int alloc_net_ping_request(u8* target_ip) {
    int slot = alloc_net_request_slot();
    if (slot < 0) {
        return -1;
    }
    g_net_ping_requests[slot].is_dns = false;
    int j = 0;
    while (j < 4) {
        g_net_ping_requests[slot].target_ip[j] = target_ip[j];
        j = j + 1;
    }
    return slot;
}

int alloc_net_dns_request(const char* hostname) {
    int slot = alloc_net_request_slot();
    if (slot < 0) {
        return -1;
    }
    g_net_ping_requests[slot].is_dns = true;
    int j = 0;
    while (j < 63 && hostname[j] != 0) {
        g_net_ping_requests[slot].hostname[j] = hostname[j];
        j = j + 1;
    }
    g_net_ping_requests[slot].hostname[j] = 0;
    return slot;
}

void free_net_ping_request(int slot_index) {
    g_net_ping_requests[slot_index].used = false;
}

void net_worker_entry(void) {
    for (;;) {
        int i = 0;
        while (i < NET_PING_SLOTS) {
            if (g_net_ping_requests[i].used && !g_net_ping_requests[i].done) {
                if (g_net_ping_requests[i].is_dns) {
                    bool ok = dns_resolve_a(&g_net_ping_requests[i].hostname[0], &g_net_ping_requests[i].resolved_ip[0]);
                    g_net_ping_requests[i].ok = ok;
                } else {
                    bool ok = icmp_ping(&g_net_ping_requests[i].target_ip[0], PING_IDENTIFIER, (u16) i);
                    g_net_ping_requests[i].ok = ok;
                }
                g_net_ping_requests[i].done = true;
            }
            i = i + 1;
        }
        yield();
    }
}
