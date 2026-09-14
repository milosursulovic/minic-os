#pragma once

#include "../../../types.h"
#include "../../../kernel/net/rtw89/dot11.h"
#include "../../../kernel/net/rtw89/wifi_manager.h"

#pragma GCC visibility push(hidden)

// Backs syscalls 101-104 (async WiFi scan/connect, issue+wait) - own
// worker task, same real shape as proc/ipc/net_request/net_request.c's
// existing ping/DNS pattern: a scan (~450ms, 3 channels) or a connect
// (auth+assoc+WPA2 handshake, up to ~1-2s) would otherwise stall this
// cooperative kernel's whole system for that long if run directly
// inside the syscall handler - the worker task + wait()/yield() pair
// lets every other task keep running meanwhile.

#define WIFI_REQUEST_SLOTS 2
#define WIFI_REQUEST_SCAN 0
#define WIFI_REQUEST_CONNECT 1

typedef struct {
    bool used;
    bool done;
    u8 op;  // WIFI_REQUEST_SCAN or WIFI_REQUEST_CONNECT

    // WIFI_REQUEST_CONNECT input:
    char ssid[33];
    char passphrase[64];
    u32 passphrase_len;

    // WIFI_REQUEST_SCAN output:
    wifi_network scan_results[WIFI_SCAN_MAX_NETWORKS];
    int scan_count;

    // WIFI_REQUEST_CONNECT output:
    wifi_state connect_result;
} wifi_request;

extern wifi_request g_wifi_requests[WIFI_REQUEST_SLOTS];

int alloc_wifi_scan_request(void);
int alloc_wifi_connect_request(const char* ssid, const char* passphrase, u32 passphrase_len);
void free_wifi_request(int slot_index);
void wifi_worker_entry(void);

#pragma GCC visibility pop
