#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

extern u64 g_tick_count;

// Called from boot/interrupts.s's isr_common_stub for every vector it
// knows about, and from isr_syscall for vector 0x80 (see syscall.c) -
// an ordinary function, nothing interrupt-specific about its body.
// saved_rip is whatever the CPU pushed as the interrupted context's RIP -
// invaluable for pinning down exactly which instruction a GPF/page fault
// came from instead of guessing from the faulting address alone.
void interrupt_handler(u64 vector, u64 error_code, u64 saved_rip);

u64 read_cr2(void);

#pragma GCC visibility pop
