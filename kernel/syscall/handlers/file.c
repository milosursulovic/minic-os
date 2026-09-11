#include "file.h"
#include "../../sched/task.h"
#include "../../../proc/ipc/object/object.h"
#include "../../../proc/ipc/file/file.h"
#include "../../../proc/process.h"

bool syscall_file(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    if (num == 44) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        char* path = (char*) a1;
        int access = (int) a2;
        int slot = file_object_open(path, access, g_processes[caller_process].uid);
        if (slot < 0) {
            // Preserve the real, distinct failure code (-1 not-found/
            // other, -2 permission-denied) through the u64 result -
            // proc/posix/posix.h reads it back as (i64) to set a real
            // errno instead of guessing.
            *result = (u64) (i64) slot;
            return true;
        }
        int obj_index = alloc_object(OBJ_FILE, slot);
        if (obj_index < 0) {
            file_object_close(slot);
            *result = (u64) -1;
            return true;
        }
        int rights = 0;
        if (access != FILE_ACCESS_WRONLY) {
            rights = rights | RIGHT_READ;
        }
        if (access != FILE_ACCESS_RDONLY) {
            rights = rights | RIGHT_WRITE;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, rights);
        if (handle_idx < 0) {
            free_object(obj_index);
            file_object_close(slot);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 45) {
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
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_READ) == 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_FILE) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        u8* buf = (u8*) a2;
        int n = file_object_read(slot, buf, (u32) a3);
        *result = (u64) n;
        return true;
    }
    if (num == 46) {
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
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_WRITE) == 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_FILE) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        u8* data = (u8*) a2;
        int n = file_object_write(slot, data, (u32) a3);
        *result = (u64) n;
        return true;
    }
    if (num == 47) {
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
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_READ) == 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_FILE) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        i64 new_pos = file_object_seek(slot, (i64) a2, (int) a3);
        *result = (u64) new_pos;
        return true;
    }
    if (num == 48) {
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
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_FILE) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        bool ok = file_object_close(slot);
        free_object(obj_index);
        free_handle(caller_process, handle_idx);
        *result = ok ? 0 : (u64) -1;
        return true;
    }
    return false;
}
