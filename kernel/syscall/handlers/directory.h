#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real Directory object type (87-89, Faza I point 2 item 5).
bool syscall_directory(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
