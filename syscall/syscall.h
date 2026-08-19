#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

u64 syscall_dispatch(u64 num, u64 a1, u64 a2, u64 a3);

#pragma GCC visibility pop
