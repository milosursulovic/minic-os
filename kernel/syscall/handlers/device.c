#include "device.h"
#include "../../sched/task.h"
#include "../../../proc/ipc/object/object.h"
#include "../../drivers/device_manager/device_manager.h"

typedef struct __attribute__((packed)) {
    int index;
    char* name_out;
    int* category_out;
    u32* info_out;
} device_list_args;

typedef struct __attribute__((packed)) {
    char* name_out;
    int* category_out;
    u32* info_out;
} device_query_args;

bool syscall_device(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    (void) a3;
    if (num == 64) {
        device_list_args* args = (device_list_args*) a1;
        bool ok = device_manager_get(args->index, args->name_out, args->category_out, args->info_out);
        *result = (u64) ok;
        return true;
    }
    if (num == 90) {
        // device_open - Faza I point 2 item 5. data_index is the device's
        // own index directly into g_devices[] (see OBJ_DEVICE's own
        // comment, object.h).
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int index = (int) a1;
        char dummy_name[32];
        int dummy_category;
        u32 dummy_info;
        if (!device_manager_get(index, &dummy_name[0], &dummy_category, &dummy_info)) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = alloc_object(OBJ_DEVICE, index);
        if (obj_index < 0) {
            *result = (u64) -1;
            return true;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, RIGHT_QUERY);
        if (handle_idx < 0) {
            free_object(obj_index);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 91) {
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
        if (g_objects[obj_index].type != OBJ_DEVICE) {
            *result = (u64) -1;
            return true;
        }
        int index = g_objects[obj_index].data_index;
        device_query_args* args = (device_query_args*) a2;
        bool ok = device_manager_get(index, args->name_out, args->category_out, args->info_out);
        *result = ok ? 0 : (u64) -1;
        return true;
    }
    if (num == 92) {
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
        if (g_objects[obj_index].type != OBJ_DEVICE) {
            *result = (u64) -1;
            return true;
        }
        free_object(obj_index);
        free_handle(caller_process, handle_idx);
        *result = 0;
        return true;
    }
    return false;
}
