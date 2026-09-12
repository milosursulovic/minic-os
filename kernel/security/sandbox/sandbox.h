#pragma once

#include "../../../types.h"

// Generalized capability-based sandbox (Faza I point 14, item 17) -
// directly generalizes item 14's own earlier RTC-driver isolation
// precedent (kernel/drivers/io_port_range/ + OBJ_IO_PORT_RANGE): a
// per-object predicate table gates a class of operations, checked by the
// syscall dispatcher before any handler runs.
//
// One real design inversion from every other OBJ_* type in this
// codebase, worth stating plainly: everywhere else, a handle's RIGHT_*
// bits GRANT permission - possessing the handle lets you do more. Here,
// possessing an OBJ_SANDBOX handle instead RESTRICTS - a process with NO
// such handle is unrestricted (today's existing, unchanged behavior); one
// that HOLDS the handle is denied whatever syscalls the referenced
// policy's bitmap marks. No RIGHT_* bits are used on this handle type -
// possession alone is the restriction.
//
// Automatically granted (see proc/process.c's spawn_process_from_path()
// and kernel/syscall/handlers/process.c's spawn_builtin, syscall 11) to
// every executable that passes exec_sign.h's signature check - real
// least-privilege-by-default for anything loaded from writable storage.
// Builtins (kmain.c's own trusted, unsigned spawn_process() calls) never
// get one - unrestricted, same as before this item existed.

#pragma GCC visibility push(hidden)

#define SANDBOX_POLICY_SLOTS 8

typedef struct {
    bool used;
    u64 denied_low;   // syscalls 0-63, bit N = syscall N denied
    u64 denied_high;  // syscalls 64-127, bit N = syscall (64+N) denied
} sandbox_policy;

extern sandbox_policy g_sandbox_policies[SANDBOX_POLICY_SLOTS];

int sandbox_policy_create(u64 denied_low, u64 denied_high);

// True if `process_index` holds an OBJ_SANDBOX handle whose policy denies
// `syscall_num`. False (unrestricted) for a process holding no such
// handle, or for an invalid process_index (kernel-only tasks - process
// pool.
bool sandbox_denies(int process_index, u64 syscall_num);

// The default policy granted to every signature-verified executable:
// denies the syscalls that let a sandboxed program escalate/spread -
// spawn another process from a path (6), spawn a builtin (11), register
// a new service (14), change its own uid (49), fork (95), and raw port
// I/O (99). Everything else (files, windows, channels, its own memory,
// normal execution) stays fully available - this is a deliberately
// narrow, concrete first policy, not a general policy-authoring system.
u64 sandbox_default_denied_low(void);
u64 sandbox_default_denied_high(void);

#pragma GCC visibility pop
