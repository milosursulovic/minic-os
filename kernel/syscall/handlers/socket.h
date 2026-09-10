#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real generic Socket object (59-63).
bool syscall_socket(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
