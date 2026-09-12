// See sandbox.h's own comment for the design (a direct generalization of
// kernel/drivers/io_port_range/'s existing capability-predicate shape).

#include "sandbox.h"
#include "../../../proc/ipc/object/object.h"
#include "../../../proc/process.h"

sandbox_policy g_sandbox_policies[SANDBOX_POLICY_SLOTS];

int sandbox_policy_create(u64 denied_low, u64 denied_high) {
    int i = 0;
    while (i < SANDBOX_POLICY_SLOTS) {
        if (!g_sandbox_policies[i].used) {
            g_sandbox_policies[i].used = true;
            g_sandbox_policies[i].denied_low = denied_low;
            g_sandbox_policies[i].denied_high = denied_high;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

bool sandbox_denies(int process_index, u64 syscall_num) {
    if (process_index < 0 || process_index >= MAX_PROCESSES) {
        return false;
    }
    int h = 0;
    while (h < HANDLES_PER_PROCESS) {
        if (g_handle_tables[process_index][h].used) {
            int obj_index = g_handle_tables[process_index][h].object_index;
            if (g_objects[obj_index].type == OBJ_SANDBOX) {
                int slot = g_objects[obj_index].data_index;
                if (syscall_num < 64) {
                    if ((g_sandbox_policies[slot].denied_low & ((u64) 1 << syscall_num)) != 0) {
                        return true;
                    }
                } else if (syscall_num < 128) {
                    if ((g_sandbox_policies[slot].denied_high & ((u64) 1 << (syscall_num - 64))) != 0) {
                        return true;
                    }
                }
            }
        }
        h = h + 1;
    }
    return false;
}

// Syscalls 6 (spawn_process_from_path), 11 (spawn_builtin), 14
// (register_service), 49 (setuid).
u64 sandbox_default_denied_low(void) {
    return ((u64) 1 << 6) | ((u64) 1 << 11) | ((u64) 1 << 14) | ((u64) 1 << 49);
}

// Syscalls 95 (fork) and 99 (port_io).
u64 sandbox_default_denied_high(void) {
    return ((u64) 1 << (95 - 64)) | ((u64) 1 << (99 - 64));
}
