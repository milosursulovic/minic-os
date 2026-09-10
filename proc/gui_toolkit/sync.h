#pragma once
#include "core.h"

// Real Event/Mutex/Timer objects (Faza I point 2) - wrap syscalls 74-82.
// Plus the generic cross-process handle-grant primitives (Faza I point
// 8, syscalls 83/84) that make sharing these (and any other object)
// with a freshly-spawned process possible. Split out of the former
// single gui_toolkit.h.

static __attribute__((unused)) int gt_event_create(void) {
    return (int) gt_syscall(74, 0, 0, 0);
}
static __attribute__((unused)) void gt_event_wait(int handle) {
    gt_syscall(75, (u64) handle, 0, 0);
}
static __attribute__((unused)) void gt_event_signal(int handle) {
    gt_syscall(76, (u64) handle, 0, 0);
}
static __attribute__((unused)) void gt_event_reset(int handle) {
    gt_syscall(77, (u64) handle, 0, 0);
}

static __attribute__((unused)) int gt_mutex_create(void) {
    return (int) gt_syscall(78, 0, 0, 0);
}
static __attribute__((unused)) void gt_mutex_lock(int handle) {
    gt_syscall(79, (u64) handle, 0, 0);
}
static __attribute__((unused)) void gt_mutex_unlock(int handle) {
    gt_syscall(80, (u64) handle, 0, 0);
}

static __attribute__((unused)) int gt_timer_create(u64 duration_ticks) {
    return (int) gt_syscall(81, duration_ticks, 0, 0);
}
static __attribute__((unused)) void gt_timer_wait(int handle) {
    gt_syscall(82, (u64) handle, 0, 0);
}

// Real cross-process handle sharing (Faza I point 8) - wraps syscall 83.
// Grants a real, valid handle for the SAME underlying object (any type)
// into target_task_index's own process, at whatever handle index its
// table happens to have free next. Returns that new handle index, or -1.
static __attribute__((unused)) int gt_handle_grant(int handle, u64 target_task_index) {
    return (int) gt_syscall(83, (u64) handle, target_task_index, 0);
}

// Atomic 3-handle grant (syscall 84) - see its own kernel-side comment
// for why a single call matters here (no ring3-observable partial-grant
// window). Returns true only if all three landed.
static __attribute__((unused)) bool gt_handle_grant3(int handle_a, int handle_b, int handle_c, u64 target_task_index) {
    u64 packed_bc = ((u64) (u32) handle_b << 32) | (u32) handle_c;
    return gt_syscall(84, (u64) handle_a, packed_bc, target_task_index) != (u64) -1;
}
