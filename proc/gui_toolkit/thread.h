#pragma once
#include "core.h"

// Real Thread object (Faza I point 3) - a second concurrent execution
// context sharing THIS process's own address space, wraps syscalls
// 71-73. Split out of the former single gui_toolkit.h.

// entry_vaddr is a real address within this same already-loaded image
// (e.g. the address of a function this program defines). Returns a real
// handle (>= 0) on success, -1 on failure - the same "small int or -1"
// shape every other gt_* wrapper returning a handle/id uses.
static __attribute__((unused)) int gt_thread_create(u64 entry_vaddr) {
    return (int) gt_syscall(71, entry_vaddr, 0, 0);
}

// Blocks until the target thread has exited. Not the same as any
// widget's *_poll() - a real, one-shot blocking wait, matching the
// kernel-side implementation (kernel/sched/task.c's own thread_join()).
static __attribute__((unused)) void gt_thread_join(int handle) {
    gt_syscall(72, (u64) handle, 0, 0);
}

// Never returns.
static __attribute__((unused)) void gt_thread_exit(void) {
    gt_syscall(73, 0, 0, 0);
}
