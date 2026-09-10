#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real persistent open-file objects (44-48).
bool syscall_file(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
