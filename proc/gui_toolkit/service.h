#pragma once
#include "core.h"

// Real Service Manager (crash-restart supervision) wrappers - syscalls
// 65-68. Split out of the former single gui_toolkit.h.

typedef struct __attribute__((packed)) {
    int index;
    char* name_out;
    u32* flags_out;
    u32* restart_count_out;
} gt_service_list_args;

// Lists one entry (0..SERVICE_SLOTS-1) of the Service Manager registry -
// wraps kernel/services/service_manager.h's service_list_entry via
// syscall 65. flags_out packs used(bit0)/running(bit1)/auto_restart(bit2).
static __attribute__((unused)) bool gt_service_list(int index, char* name_out, u32* flags_out, u32* restart_count_out) {
    gt_service_list_args args;
    args.index = index;
    args.name_out = name_out;
    args.flags_out = flags_out;
    args.restart_count_out = restart_count_out;
    return gt_syscall(65, (u64) &args, 0, 0) != 0;
}

// Wraps service_start/stop/restart via syscalls 66/67/68 - "stop" means
// "don't respawn the next exit", not a forced kill (see
// kernel/services/service_manager.h's own comment on why).
static __attribute__((unused)) bool gt_service_start(char* name) {
    return gt_syscall(66, (u64) name, 0, 0) != 0;
}
static __attribute__((unused)) bool gt_service_stop(char* name) {
    return gt_syscall(67, (u64) name, 0, 0) != 0;
}
static __attribute__((unused)) bool gt_service_restart(char* name) {
    return gt_syscall(68, (u64) name, 0, 0) != 0;
}
