#include "pipe.h"
#include "../../sched/task.h"
#include "../../../proc/ipc/object/object.h"
#include "../../../proc/ipc/pipe/pipe.h"

bool syscall_pipe(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    if (num == 52) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int raw_index = (int) a1;
        bool want_send = a2 != 0;
        int obj_index = alloc_object(OBJ_PIPE, raw_index);
        if (obj_index < 0) {
            *result = (u64) -1;
            return true;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, want_send ? RIGHT_SEND : RIGHT_RECEIVE);
        if (handle_idx < 0) {
            free_object(obj_index);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 53) {
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
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_SEND) == 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_PIPE) {
            *result = (u64) -1;
            return true;
        }
        int pipe_index = g_objects[obj_index].data_index;
        u8* data = (u8*) a2;
        u32 n = pipe_write(pipe_index, data, (u32) a3);
        *result = (u64) n;
        return true;
    }
    if (num == 54) {
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
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_RECEIVE) == 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_PIPE) {
            *result = (u64) -1;
            return true;
        }
        int pipe_index = g_objects[obj_index].data_index;
        u8* buf = (u8*) a2;
        u32 n = pipe_read(pipe_index, buf, (u32) a3);
        *result = (u64) n;
        return true;
    }
    return false;
}
