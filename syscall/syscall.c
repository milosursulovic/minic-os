// PLACEHOLDER for Stage 1-3 (interrupts/memory/scheduler) - real syscall
// dispatch (print, query-handle, vfs_read/write, spawn, channel ops)
// lands in Stage 4. boot/interrupts.s's isr_syscall unconditionally
// references this symbol (`call syscall_dispatch`) even though nothing
// triggers `int 0x80` yet at this stage, so it has to exist for the
// link to succeed.

#include "syscall.h"

u64 syscall_dispatch(u64 num, u64 a1, u64 a2, u64 a3) {
    (void) num;
    (void) a1;
    (void) a2;
    (void) a3;
    return (u64) -1;
}
