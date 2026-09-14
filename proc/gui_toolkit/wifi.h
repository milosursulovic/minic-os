#pragma once
#include "core.h"

// Real WiFi syscalls (101-105, kernel/syscall/handlers/wifi.h) - ring3
// wrappers, same gt_syscall trampoline every other proc/gui_toolkit/*.h
// file already uses. `wifi_network_info` mirrors kernel/net/rtw89/
// dot11.h's own `wifi_network` byte-for-byte (packed, same field
// types/order) - the real syscall 102 boundary contract, same
// "mirrored struct" precedent kernel/gfx/window/window.h's own
// input_event_t already established for gt_read_event.

#define WIFI_SCAN_MAX_NETWORKS 16

typedef struct __attribute__((packed)) {
    bool used;
    char ssid[33];
    u8 ssid_len;
    u8 bssid[6];
    u8 channel;
    bool has_rsn;
} wifi_network_info;

// Real wifi_state values (kernel/net/rtw89/wifi_manager.h) - kept in
// sync by hand (small, stable enum, same "no shared header across the
// ring0/ring3 build split" convention every other mirrored type here
// already accepts).
#define WIFI_STATE_IDLE 0
#define WIFI_STATE_CONNECTING 1
#define WIFI_STATE_CONNECTED 2
#define WIFI_STATE_FAILED_AUTH 3
#define WIFI_STATE_FAILED_ASSOC 4
#define WIFI_STATE_FAILED_WRONG_PASSWORD 5
#define WIFI_STATE_FAILED_TIMEOUT 6

static __attribute__((unused)) int gt_wifi_scan_start(void) {
    return (int) gt_syscall(101, 0, 0, 0);
}

static __attribute__((unused)) int gt_wifi_scan_wait(int handle, wifi_network_info* out_buf, u32 max_count) {
    return (int) gt_syscall(102, (u64) handle, (u64) out_buf, (u64) max_count);
}

static __attribute__((unused)) int gt_wifi_connect_start(const char* ssid, const char* passphrase,
                                                            u32 passphrase_len) {
    return (int) gt_syscall(103, (u64) ssid, (u64) passphrase, (u64) passphrase_len);
}

static __attribute__((unused)) int gt_wifi_connect_wait(int handle) {
    return (int) gt_syscall(104, (u64) handle, 0, 0);
}

static __attribute__((unused)) int gt_wifi_status(char* ssid_out, u32 max_len) {
    return (int) gt_syscall(105, (u64) ssid_out, (u64) max_len, 0);
}
