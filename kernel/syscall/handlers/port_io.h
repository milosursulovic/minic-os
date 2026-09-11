#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// 99 (port_io_request) - Faza I point 14, item 14: the real capability-
// gated port I/O a ring3 driver process uses in place of direct in/out
// (which CPL3 code can't execute without a per-process IOPL/TSS I/O
// bitmap this kernel doesn't set up).
bool syscall_port_io(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
