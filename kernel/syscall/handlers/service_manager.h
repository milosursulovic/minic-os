#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real Service Manager (crash-restart supervision) - 65-68. Distinct
// from handlers/process.c's 14/15 registry.
bool syscall_service_manager(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
