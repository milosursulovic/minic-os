// Backs syscalls 20-23 (async ping/DNS, issue+wait).

#include "net_request.h"
#include "../../../kernel/net/icmp/icmp.h"
#include "../../../kernel/net/dns/dns.h"
#include "../../../kernel/sched/task.h"

// Same disable_interrupts()/restore_interrupts() pattern
// kernel/sched/task.c's yield()/net_ping_request_wait() use (own copy - no
// shared header, see task.c's own comment) - protects net_worker_entry()'s
// write to the same `done` flag net_ping_request_wait()'s blocked/
// waiting_on pair watches.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

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
                u64 saved_flags = disable_interrupts();
                g_net_ping_requests[i].done = true;
                restore_interrupts(saved_flags);
            }
            i = i + 1;
        }
        yield();
    }
}
