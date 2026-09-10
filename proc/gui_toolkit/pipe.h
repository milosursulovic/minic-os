#pragma once
#include "core.h"

// Real byte-stream Pipe (proc/ipc/pipe/pipe.h) - unlike a Channel's
// single-value mailbox, a real FIFO with partial-read/partial-write
// semantics. mode 0=receive/1=send, same shape as gt_file_open's mode
// (proc/gui_toolkit/file.h). Syscalls 52-54. Split out of the former
// single gui_toolkit.h.

static __attribute__((unused)) int gt_pipe_open(int raw_index, int mode) {
    u64 result = gt_syscall(52, (u64) raw_index, (u64) mode, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

static __attribute__((unused)) int gt_pipe_write(int handle, const u8* data, u32 len) {
    return (int) gt_syscall(53, (u64) handle, (u64) data, (u64) len);
}

static __attribute__((unused)) int gt_pipe_read(int handle, u8* buf, u32 max_len) {
    return (int) gt_syscall(54, (u64) handle, (u64) buf, (u64) max_len);
}
