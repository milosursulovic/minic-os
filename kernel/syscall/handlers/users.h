#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real user-account lookup (93, Faza I point 14 item 6).
bool syscall_users(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
