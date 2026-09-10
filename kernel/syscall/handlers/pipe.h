#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real byte-stream Pipe (52-54).
bool syscall_pipe(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
