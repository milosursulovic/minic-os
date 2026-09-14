#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real WiFi syscalls (real-hardware driver arc item 5/5, Phase 8) -
// 101-105, the first free numbers after syscall 100 (window, per
// kernel/syscall/syscall.c's own real numbering, confirmed free this
// session via a repo-wide grep of every existing `num ==` check).
//
// 101 wifi_scan_start() -> handle
// 102 wifi_scan_wait(handle, wifi_network* out_buf, u32 max_count) -> count
// 103 wifi_connect_start(const char* ssid, const char* passphrase, u32 passphrase_len) -> handle
// 104 wifi_connect_wait(handle) -> wifi_state (see wifi_manager.h)
// 105 wifi_status(char* ssid_out, u32 max_len) -> wifi_state (plain poll, no handle)
bool syscall_wifi(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
