#pragma once
#include "core.h"

// Real generic Socket object (proc/ipc/socket/socket.h) - wraps
// kernel/net/tcp/tcp.c's real listen/accept, not just a client fetch.
// Syscalls 59-63. Split out of the former single gui_toolkit.h.

static __attribute__((unused)) int gt_socket_listen(u16 port) {
    u64 result = gt_syscall(59, (u64) port, 0, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

// Fixed internal accept timeout (kernel/syscall/handlers/socket.c,
// matches gt_pipe/gt_file's own shapes - 3 syscall args only).
static __attribute__((unused)) int gt_socket_accept(int handle) {
    u64 result = gt_syscall(60, (u64) handle, 0, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

static __attribute__((unused)) int gt_socket_send(int handle, const u8* data, u16 len) {
    return (int) gt_syscall(61, (u64) handle, (u64) data, (u64) len);
}

static __attribute__((unused)) int gt_socket_receive(int handle, u8* buf, u32 max_len) {
    return (int) gt_syscall(62, (u64) handle, (u64) buf, (u64) max_len);
}

static __attribute__((unused)) bool gt_socket_close(int handle) {
    return gt_syscall(63, (u64) handle, 0, 0) != (u64) -1;
}
