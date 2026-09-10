#pragma once
#include "core.h"

// Real frame-backed SharedMemory (proc/ipc/shared_memory/shared_memory.h)
// - RIGHT_MAP, the roadmap's own literal "Handle<File> READ/WRITE/MAP".
// Syscalls 55-57. Split out of the former single gui_toolkit.h.

static __attribute__((unused)) int gt_shm_create(u32 size) {
    u64 result = gt_syscall(55, (u64) size, 0, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

// Maps into the CALLER's own address space at vaddr.
static __attribute__((unused)) bool gt_shm_map(int handle, u64 vaddr) {
    return gt_syscall(56, (u64) handle, vaddr, 0) != (u64) -1;
}

// Maps into a DIFFERENT process's address space (e.g. a child just
// spawned via gt_syscall(6, ...)/process_spawn, which hands back its
// real task_index) - deliberately permissive, no ownership check on the
// target.
static __attribute__((unused)) bool gt_shm_map_into(int handle, u64 target_task_index, u64 vaddr) {
    return gt_syscall(57, (u64) handle, target_task_index, vaddr) != (u64) -1;
}
