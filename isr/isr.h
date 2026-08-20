#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

extern u64 g_tick_count;

// saved_rip is the interrupted context's RIP, for GPF/page fault diagnostics.
void interrupt_handler(u64 vector, u64 error_code, u64 saved_rip);

u64 read_cr2(void);

#pragma GCC visibility pop
