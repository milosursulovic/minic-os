#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Base for a real per-spawn ASLR load address (kernel/lib/rand.h) - safe
// to reuse across processes, each gets its own cloned address space.
// Shared between handlers/process.c (syscall 11, builtin spawn) and
// handlers/system.c (syscall 41, GUI app spawn) - the only two syscalls
// that spawn a compiled-in program image.
#define BUILTIN_LOAD_BASE 0x80000000

#pragma GCC visibility pop
