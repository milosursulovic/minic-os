#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Channel send/receive/open (7/8/9) + structured-payload variants
// (85/86, Faza I point 8 item 4).
bool syscall_channel(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
