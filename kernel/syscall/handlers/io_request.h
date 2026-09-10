#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Async file read/write + wait (16/17 read+wait, 18/19 write+wait).
bool syscall_io_request(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
