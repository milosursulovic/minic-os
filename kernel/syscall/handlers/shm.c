#include "shm.h"
#include "../../sched/task.h"
#include "../../../proc/ipc/object/object.h"
#include "../../../proc/ipc/shared_memory/shared_memory.h"
#include "../../../proc/process.h"

bool syscall_shm(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    if (num == 55) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int slot = alloc_shared_memory((u32) a1);
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = alloc_object(OBJ_SHARED_MEMORY, slot);
        if (obj_index < 0) {
            *result = (u64) -1;
            return true;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, RIGHT_MAP);
        if (handle_idx < 0) {
            free_object(obj_index);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 56) {
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
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_MAP) == 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_SHARED_MEMORY) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        bool ok = shared_memory_map(slot, g_processes[caller_process].cr3, a2);
        *result = ok ? 0 : (u64) -1;
        return true;
    }
    if (num == 57) {
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
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_MAP) == 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_SHARED_MEMORY) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        int target_task_index = (int) a2;
        int target_process = g_tasks[target_task_index].process_index;
        if (target_process < 0) {
            *result = (u64) -1;
            return true;
        }
        bool ok = shared_memory_map(slot, g_processes[target_process].cr3, a3);
        *result = ok ? 0 : (u64) -1;
        return true;
    }
    return false;
}
