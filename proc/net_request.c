// Async ICMP ping: net_ping_async() (syscall 20) issues a request and
// returns immediately; a dedicated kernel worker task performs the real
// icmp_ping() concurrently; net_ping_wait() (syscall 21) blocks the
// caller only once it actually needs the result.

#include "net_request.h"
#include "../net/icmp.h"
#include "../sched/task.h"

net_ping_request g_net_ping_requests[NET_PING_SLOTS];

static const u16 PING_IDENTIFIER = 0x5150;  // "PQ" - fixed, distinguishes this worker's own pings

int alloc_net_ping_request(u8* target_ip) {
    int i = 0;
    while (i < NET_PING_SLOTS) {
        if (!g_net_ping_requests[i].used) {
            int j = 0;
            while (j < 4) {
                g_net_ping_requests[i].target_ip[j] = target_ip[j];
                j = j + 1;
            }
            g_net_ping_requests[i].used = true;
            g_net_ping_requests[i].done = false;
            g_net_ping_requests[i].ok = false;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

void free_net_ping_request(int slot_index) {
    g_net_ping_requests[slot_index].used = false;
}

void net_worker_entry(void) {
    for (;;) {
        int i = 0;
        while (i < NET_PING_SLOTS) {
            if (g_net_ping_requests[i].used && !g_net_ping_requests[i].done) {
                bool ok = icmp_ping(&g_net_ping_requests[i].target_ip[0], PING_IDENTIFIER, (u16) i);
                g_net_ping_requests[i].ok = ok;
                g_net_ping_requests[i].done = true;
            }
            i = i + 1;
        }
        yield();
    }
}
