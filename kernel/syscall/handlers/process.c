#include "process.h"
#include "common.h"
#include "../../sched/task.h"
#include "../../../proc/process.h"
#include "../../../proc/ipc/object/object.h"
#include "../../fs/vfs/vfs.h"
#include "../../mm/paging/paging.h"
#include "../../lib/rand.h"

#pragma GCC visibility push(hidden)
extern u8 g_hello_service_prog_start;
extern u8 g_hello_service_prog_end;
#pragma GCC visibility pop

// Index 0 is the one fixed compile-time entry; indices 1+ map to
// runtime-registered slots (register_service, syscall 14) - the
// registry isn't only the compile-time table anymore.
#define REGISTERED_SERVICE_SLOTS 4
#define REGISTERED_SERVICE_MAX_BYTES 16384

static u8 g_registered_service_buf[REGISTERED_SERVICE_SLOTS][REGISTERED_SERVICE_MAX_BYTES];
static u32 g_registered_service_len[REGISTERED_SERVICE_SLOTS];
static bool g_registered_service_used[REGISTERED_SERVICE_SLOTS];

// Not a static array of &symbol pointers - that needs an absolute 64-bit
// relocation the ELF32 build container can't represent.
static bool builtin_program_bounds(int index, u8** start_out, u8** end_out) {
    if (index == 0) {
        *start_out = &g_hello_service_prog_start;
        *end_out = &g_hello_service_prog_end;
        return true;
    }
    int slot = index - 1;
    if (slot >= 0 && slot < REGISTERED_SERVICE_SLOTS && g_registered_service_used[slot]) {
        *start_out = &g_registered_service_buf[slot][0];
        *end_out = &g_registered_service_buf[slot][g_registered_service_len[slot]];
        return true;
    }
    return false;
}

bool syscall_process(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    if (num == 3) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            *result = (u64) -1;
            return true;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            *result = (u64) -1;
            return true;
        }
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_QUERY) == 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type == OBJ_PROCESS) {
            int proc_idx = g_objects[obj_index].data_index;
            if (!g_processes[proc_idx].used) {
                *result = (u64) -1;  // exited
                return true;
            }
            *result = (u64) g_processes[proc_idx].task_index;
            return true;
        }
        *result = (u64) -1;
        return true;
    }
    if (num == 6) {
        char* path = (char*) a1;
        int proc_index = spawn_process_from_path(path, a2, a3);
        if (proc_index < 0) {
            *result = (u64) -1;
            return true;
        }
        *result = (u64) g_processes[proc_index].task_index;
        return true;
    }
    if (num == 10) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int target_task = (int) a1;
        if (target_task < 0 || target_task >= g_task_count) {
            *result = (u64) -1;
            return true;
        }
        int target_process = g_tasks[target_task].process_index;
        if (target_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = alloc_object(OBJ_PROCESS, target_process);
        if (obj_index < 0) {
            *result = (u64) -1;
            return true;
        }
        int granted_rights = ((int) a2) & RIGHT_QUERY;
        int handle_idx = alloc_handle(caller_process, obj_index, granted_rights);
        if (handle_idx < 0) {
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 11) {
        u8* start;
        u8* end;
        if (!builtin_program_bounds((int) a1, &start, &end)) {
            *result = (u64) -1;
            return true;
        }
        u64 load_vaddr = randomize_load_vaddr(BUILTIN_LOAD_BASE);
        int proc_index = spawn_process(start, end, load_vaddr, load_vaddr + 0x20000);
        if (proc_index < 0) {
            *result = (u64) -1;
            return true;
        }
        *result = (u64) g_processes[proc_index].task_index;
        return true;
    }
    if (num == 12) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process >= 0) {
            g_processes[caller_process].used = false;
            free_address_space(g_processes[caller_process].cr3);
            int h = 0;
            while (h < HANDLES_PER_PROCESS) {
                if (g_handle_tables[caller_process][h].used) {
                    free_object(g_handle_tables[caller_process][h].object_index);
                    free_handle(caller_process, h);
                }
                h = h + 1;
            }
        }
        g_tasks[g_current_task].used = false;
        yield();
        *result = 0;  // never actually reached - yield() never switches back to an exited task
        return true;
    }
    if (num == 14) {
        char* path = (char*) a1;
        int slot = -1;
        int i = 0;
        while (i < REGISTERED_SERVICE_SLOTS) {
            if (!g_registered_service_used[i]) {
                slot = i;
                break;
            }
            i = i + 1;
        }
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int caller_process = g_tasks[g_current_task].process_index;
        u8 caller_uid = caller_process < 0 ? 0 : g_processes[caller_process].uid;
        int n = vfs_read(path, &g_registered_service_buf[slot][0], REGISTERED_SERVICE_MAX_BYTES, caller_uid);
        if (n < 0) {
            *result = (u64) -1;
            return true;
        }
        g_registered_service_used[slot] = true;
        g_registered_service_len[slot] = (u32) n;
        *result = (u64) (slot + 1);  // index 0 stays reserved for the compile-time entry
        return true;
    }
    if (num == 15) {
        int index = (int) a1;
        int slot = index - 1;
        if (slot < 0 || slot >= REGISTERED_SERVICE_SLOTS) {
            *result = (u64) -1;
            return true;
        }
        if (!g_registered_service_used[slot]) {
            *result = (u64) -1;
            return true;
        }
        g_registered_service_used[slot] = false;
        g_registered_service_len[slot] = 0;
        *result = 0;
        return true;
    }
    if (num == 49) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        g_processes[caller_process].uid = (u8) a1;
        *result = 0;
        return true;
    }
    return false;
}
