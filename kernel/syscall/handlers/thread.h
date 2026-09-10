#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real Thread object (71-73).
bool syscall_thread(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
