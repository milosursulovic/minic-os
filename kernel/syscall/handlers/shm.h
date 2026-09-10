#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real frame-backed SharedMemory (55-57).
bool syscall_shm(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
