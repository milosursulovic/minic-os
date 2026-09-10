#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Device Manager registry - legacy raw device_list (64) plus the real
// handle+rights-gated Device object (90-92, Faza I point 2 item 5).
bool syscall_device(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
