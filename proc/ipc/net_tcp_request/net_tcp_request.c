// Backs syscalls 24/25 (async TCP fetch, issue+wait).

#include "net_tcp_request.h"
#include "../../../kernel/net/tcp/tcp.h"
#include "../../../kernel/sched/task.h"

// Same disable_interrupts()/restore_interrupts() pattern
// kernel/sched/task.c's yield()/net_tcp_request_wait() use (own copy - no
// shared header, see task.c's own comment) - protects tcp_worker_entry()'s
// write to the same `done` flag net_tcp_request_wait()'s blocked/
// waiting_on pair watches.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

net_tcp_request g_net_tcp_requests[TCP_REQUEST_SLOTS];

int alloc_net_tcp_request(u8* target_ip, u16 target_port, const char* payload, u16 payload_len) {
    int i = 0;
    while (i < TCP_REQUEST_SLOTS) {
        if (!g_net_tcp_requests[i].used) {
            int j = 0;
            while (j < 4) {
                g_net_tcp_requests[i].target_ip[j] = target_ip[j];
                j = j + 1;
            }
            g_net_tcp_requests[i].target_port = target_port;
            u16 n = payload_len;
            if (n > TCP_REQUEST_PAYLOAD_SIZE) {
                n = TCP_REQUEST_PAYLOAD_SIZE;
            }
            int k = 0;
            while (k < (int) n) {
                g_net_tcp_requests[i].payload[k] = payload[k];
                k = k + 1;
            }
            g_net_tcp_requests[i].payload_len = n;
            g_net_tcp_requests[i].used = true;
            g_net_tcp_requests[i].done = false;
            g_net_tcp_requests[i].ok = false;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

void free_net_tcp_request(int slot_index) {
    g_net_tcp_requests[slot_index].used = false;
}

void tcp_worker_entry(void) {
    for (;;) {
        int i = 0;
        while (i < TCP_REQUEST_SLOTS) {
            if (g_net_tcp_requests[i].used && !g_net_tcp_requests[i].done) {
                u32 response_len = 0;
                bool ok = tcp_fetch(&g_net_tcp_requests[i].target_ip[0], g_net_tcp_requests[i].target_port,
                                     &g_net_tcp_requests[i].payload[0], g_net_tcp_requests[i].payload_len,
                                     &g_net_tcp_requests[i].response[0], TCP_REQUEST_RESPONSE_SIZE, &response_len);
                g_net_tcp_requests[i].response_len = response_len;
                g_net_tcp_requests[i].ok = ok;
                u64 saved_flags = disable_interrupts();
                g_net_tcp_requests[i].done = true;
                restore_interrupts(saved_flags);
            }
            i = i + 1;
        }
        yield();
    }
}
