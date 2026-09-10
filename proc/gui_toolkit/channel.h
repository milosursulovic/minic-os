#pragma once
#include "core.h"

// Faza I point 8 item 4: real structured payload over a Channel, beyond
// one raw u64 (syscalls 85/86). Split out of the former single
// gui_toolkit.h.

static __attribute__((unused)) bool gt_channel_send_msg(int handle, const void* data, u32 len) {
    return gt_syscall(85, (u64) handle, (u64) data, (u64) len) != (u64) -1;
}

static __attribute__((unused)) u32 gt_channel_receive_msg(int handle, void* buf, u32 max_len) {
    return (u32) gt_syscall(86, (u64) handle, (u64) buf, (u64) max_len);
}
