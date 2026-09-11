#pragma once
#include "core.h"

// Real high-level Process.spawn()/.wait() native API (Faza I point 3,
// item 9) - consolidates what ring3prog.c's own local process/
// process_handle structs + do_syscall wrappers already do inline into
// one real, reusable module, matching every other subsystem's own gt_
// wrapper here. Existing ring3prog.c call sites keep their own local
// copies untouched (already working) - this is a new, additive module
// for new code to use.

// Spawns a program already installed on disk (wraps syscall 6). Returns
// the new task_index, or -1 on failure.
static __attribute__((unused)) int gt_process_spawn(const char* path, u64 load_vaddr, u64 stack_vaddr) {
    u64 result = gt_syscall(6, (u64) path, load_vaddr, stack_vaddr);
    return result == (u64) -1 ? -1 : (int) result;
}

// Opens a real handle to another task's owning process (wraps syscall
// 10) - rights is a RIGHT_* bitmask (proc/ipc/object/object.h); only
// RIGHT_QUERY is meaningful for a Process object today. Returns the new
// handle index, or -1.
static __attribute__((unused)) int gt_process_open(int task_index, int rights) {
    u64 result = gt_syscall(10, (u64) task_index, (u64) rights, 0);
    return result == (u64) -1 ? -1 : (int) result;
}

// Returns the target's own task_index if it's still running, or -1 if
// it has exited (or the handle lacks RIGHT_QUERY) - wraps syscall 3.
static __attribute__((unused)) u64 gt_process_query(int handle) {
    return gt_syscall(3, (u64) handle, 0, 0);
}

// Real blocking wait (this item's own new capability, syscall 96) -
// blocks until the target process actually exits, same cooperative
// busy-yield shape kernel/sched/task.c's own thread_join() already
// established for threads. Needs a handle with RIGHT_QUERY (same
// rights gt_process_query() needs) - returns false if the handle itself
// is invalid/lacks rights, true once the wait completes.
static __attribute__((unused)) bool gt_process_wait(int handle) {
    return gt_syscall(96, (u64) handle, 0, 0) != (u64) -1;
}
