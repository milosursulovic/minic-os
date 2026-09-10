#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real copy-on-write fork() (Faza I point 4, item 8) - 95 process_fork.
bool syscall_fork(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
