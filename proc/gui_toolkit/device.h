#pragma once
#include "core.h"

// Device Manager registry wrappers - legacy raw-index gt_device_list()
// (syscall 64) plus the real, handle+rights-gated Device object added
// for Faza I point 2 item 5 (syscalls 90-92). Split out of the former
// single gui_toolkit.h.

typedef struct __attribute__((packed)) {
    int index;
    char* name_out;
    int* category_out;
    u32* info_out;
} gt_device_list_args;

// Lists one entry (0..MAX_DEVICES-1) of the Device Manager registry -
// wraps kernel/drivers/device_manager/device_manager.h's device_manager_get
// via syscall 64. Returns false for an out-of-range or unused slot.
static __attribute__((unused)) bool gt_device_list(int index, char* name_out, int* category_out, u32* info_out) {
    gt_device_list_args args;
    args.index = index;
    args.name_out = name_out;
    args.category_out = category_out;
    args.info_out = info_out;
    return gt_syscall(64, (u64) &args, 0, 0) != 0;
}

static __attribute__((unused)) int gt_device_open(int index) {
    u64 result = gt_syscall(90, (u64) index, 0, 0);
    return result == (u64) -1 ? -1 : (int) result;
}

typedef struct __attribute__((packed)) {
    char* name_out;
    int* category_out;
    u32* info_out;
} gt_device_query_args;

static __attribute__((unused)) bool gt_device_query(int handle, char* name_out, int* category_out, u32* info_out) {
    gt_device_query_args args;
    args.name_out = name_out;
    args.category_out = category_out;
    args.info_out = info_out;
    return gt_syscall(91, (u64) handle, (u64) &args, 0) != (u64) -1;
}

static __attribute__((unused)) bool gt_device_close(int handle) {
    return gt_syscall(92, (u64) handle, 0, 0) != (u64) -1;
}
