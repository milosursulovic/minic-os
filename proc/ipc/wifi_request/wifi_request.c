// Backs syscalls 101-104 (async WiFi scan/connect, issue+wait).

#include "wifi_request.h"
#include "../../../kernel/sched/task.h"

// Same disable_interrupts()/restore_interrupts() pattern proc/ipc/
// net_request/net_request.c's own worker uses (own copy - no shared
// header, matching that file's own precedent) - protects the write to
// `done`, the flag wifi_request_wait()'s blocked/waiting_on pair
// watches.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

wifi_request g_wifi_requests[WIFI_REQUEST_SLOTS];

static int alloc_wifi_request_slot(void) {
    int i = 0;
    while (i < WIFI_REQUEST_SLOTS) {
        if (!g_wifi_requests[i].used) {
            g_wifi_requests[i].used = true;
            g_wifi_requests[i].done = false;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

int alloc_wifi_scan_request(void) {
    int slot = alloc_wifi_request_slot();
    if (slot < 0) {
        return -1;
    }
    g_wifi_requests[slot].op = WIFI_REQUEST_SCAN;
    return slot;
}

int alloc_wifi_connect_request(const char* ssid, const char* passphrase, u32 passphrase_len) {
    int slot = alloc_wifi_request_slot();
    if (slot < 0) {
        return -1;
    }
    g_wifi_requests[slot].op = WIFI_REQUEST_CONNECT;
    int j = 0;
    while (j < 32 && ssid[j] != 0) {
        g_wifi_requests[slot].ssid[j] = ssid[j];
        j = j + 1;
    }
    g_wifi_requests[slot].ssid[j] = 0;
    if (passphrase_len > 63) {
        passphrase_len = 63;
    }
    j = 0;
    while (j < (int) passphrase_len) {
        g_wifi_requests[slot].passphrase[j] = passphrase[j];
        j = j + 1;
    }
    g_wifi_requests[slot].passphrase[j] = 0;
    g_wifi_requests[slot].passphrase_len = passphrase_len;
    return slot;
}

void free_wifi_request(int slot_index) {
    g_wifi_requests[slot_index].used = false;
}

void wifi_worker_entry(void) {
    for (;;) {
        int i = 0;
        while (i < WIFI_REQUEST_SLOTS) {
            if (g_wifi_requests[i].used && !g_wifi_requests[i].done) {
                if (g_wifi_requests[i].op == WIFI_REQUEST_SCAN) {
                    g_wifi_requests[i].scan_count =
                        wifi_manager_scan(&g_wifi_requests[i].scan_results[0], WIFI_SCAN_MAX_NETWORKS);
                } else {
                    g_wifi_requests[i].connect_result = wifi_manager_connect(
                        &g_wifi_requests[i].ssid[0], &g_wifi_requests[i].passphrase[0],
                        g_wifi_requests[i].passphrase_len);
                }
                u64 saved_flags = disable_interrupts();
                g_wifi_requests[i].done = true;
                restore_interrupts(saved_flags);
            }
            i = i + 1;
        }
        yield();
    }
}
