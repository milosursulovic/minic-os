#include "directory.h"
#include "../../sched/task.h"
#include "../../../proc/ipc/object/object.h"
#include "../../../proc/ipc/directory/directory.h"

typedef struct __attribute__((packed)) {
    char* name_out;
    u32* size_out;
    bool* is_dir_out;
} directory_read_args;

bool syscall_directory(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    (void) a3;
    if (num == 87) {
        // directory_open - Faza I point 2 item 5. Same open->alloc_object->
        // alloc_handle shape as syscall 44 (file_object_open).
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        char* dir_path = (char*) a1;
        int slot = directory_object_open(dir_path);
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = alloc_object(OBJ_DIRECTORY, slot);
        if (obj_index < 0) {
            directory_object_close(slot);
            *result = (u64) -1;
            return true;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, RIGHT_READ);
        if (handle_idx < 0) {
            free_object(obj_index);
            directory_object_close(slot);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 88) {
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
        if (g_objects[obj_index].type != OBJ_DIRECTORY) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        directory_read_args* args = (directory_read_args*) a2;
        bool ok = directory_object_read_next(slot, args->name_out, args->size_out, args->is_dir_out);
        *result = ok ? 0 : (u64) -1;
        return true;
    }
    if (num == 89) {
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
        if (g_objects[obj_index].type != OBJ_DIRECTORY) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        bool ok = directory_object_close(slot);
        free_object(obj_index);
        free_handle(caller_process, handle_idx);
        *result = ok ? 0 : (u64) -1;
        return true;
    }
    return false;
}
