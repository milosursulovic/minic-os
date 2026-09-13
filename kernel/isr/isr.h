#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

extern u64 g_tick_count;
// Real Shift/Ctrl/Alt state (Faza II point 17) - live, global, like a
// real OS's modifier state; shell/editor/editor.c reads g_shift_down
// directly for the same shift-aware character mapping isr.c's own
// console/focused-window paths use.
extern bool g_shift_down;
extern bool g_ctrl_down;
extern bool g_alt_down;

// saved_rip is the interrupted context's RIP, for GPF/page fault diagnostics.
void interrupt_handler(u64 vector, u64 error_code, u64 saved_rip);

u64 read_cr2(void);

#pragma GCC visibility pop
