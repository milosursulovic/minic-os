#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Ticks/term-scrollback/sys-info/GUI-app-spawn/time/date - 35, 36, 40,
// 41, 42, 43.
bool syscall_system(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
