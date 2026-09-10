#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Generic, object-system-wide syscalls, not tied to one resource type:
// 1 (debug print), 13 (handle_close), 83/84 (handle_grant/grant3).
bool syscall_core(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
