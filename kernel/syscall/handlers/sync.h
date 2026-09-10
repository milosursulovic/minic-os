#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real Event/Mutex/Timer objects (74-82).
bool syscall_sync(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
