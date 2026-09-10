#pragma once
#include "core.h"

// Faza I point 2 item 5: real Directory object type (syscalls 87-89) -
// handle+rights gated, unlike the older raw-index gt_fs_list()
// (proc/gui_toolkit/vfs.h). Split out of the former single
// gui_toolkit.h.

static __attribute__((unused)) int gt_directory_open(const char* dir_path) {
    u64 result = gt_syscall(87, (u64) dir_path, 0, 0);
    return result == (u64) -1 ? -1 : (int) result;
}

typedef struct __attribute__((packed)) {
    char* name_out;
    u32* size_out;
    bool* is_dir_out;
} gt_directory_read_args;

static __attribute__((unused)) bool gt_directory_read_next(int handle, char* name_out, u32* size_out, bool* is_dir_out) {
    gt_directory_read_args args;
    args.name_out = name_out;
    args.size_out = size_out;
    args.is_dir_out = is_dir_out;
    return gt_syscall(88, (u64) handle, (u64) &args, 0) != (u64) -1;
}

static __attribute__((unused)) bool gt_directory_close(int handle) {
    return gt_syscall(89, (u64) handle, 0, 0) != (u64) -1;
}
