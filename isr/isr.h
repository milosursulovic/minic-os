#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

extern u64 g_tick_count;

// Called from boot/interrupts.s's isr_common_stub for every vector it
// knows about, and from isr_syscall for vector 0x80 (see syscall.c) -
// an ordinary function, nothing interrupt-specific about its body.
void interrupt_handler(u64 vector, u64 error_code);

u64 read_cr2(void);

#pragma GCC visibility pop
