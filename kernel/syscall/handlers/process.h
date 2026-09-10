#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Process lifecycle - 3 (query handle), 6 (spawn from VFS path), 10
// (open_process), 11 (spawn_builtin), 12 (process_exit), 49
// (sys_setuid), plus the builtin-service registry (14/15
// register/unregister_service) - kept together since 11's own
// builtin_program_bounds() directly reads the exact state 14/15 write.
bool syscall_process(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
