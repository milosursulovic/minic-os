#include "io_request.h"
#include "../../sched/task.h"
#include "../../../proc/ipc/object/object.h"
#include "../../../proc/ipc/io_request/io_request.h"

bool syscall_io_request(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    if (num == 16) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        char* path = (char*) a1;
        int slot = alloc_io_request(path);
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = alloc_object(OBJ_IO_REQUEST, slot);
        if (obj_index < 0) {
            free_io_request(slot);
            *result = (u64) -1;
            return true;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, 0);
        if (handle_idx < 0) {
            free_object(obj_index);
            free_io_request(slot);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 17) {
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
        if (g_objects[obj_index].type != OBJ_IO_REQUEST) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        if (g_io_requests[slot].is_write) {
            *result = (u64) -1;  // wrong wait syscall for a write-issued handle
            return true;
        }
        io_request_wait(slot);
        u8* buf = (u8*) a2;
        u32 max_len = (u32) a3;
        int io_result = g_io_requests[slot].result;
        if (io_result > 0) {
            u32 n = (u32) io_result;
            if (n > max_len) {
                n = max_len;
            }
            u32 i = 0;
            while (i < n) {
                buf[i] = g_io_requests[slot].buffer[i];
                i = i + 1;
            }
        }
        free_io_request(slot);
        free_object(obj_index);
        free_handle(caller_process, handle_idx);
        *result = (u64) io_result;
        return true;
    }
    if (num == 18) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        char* path = (char*) a1;
        u8* payload = (u8*) a2;
        u32 payload_len = (u32) a3;
        int slot = alloc_io_write_request(path, payload, payload_len);
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = alloc_object(OBJ_IO_REQUEST, slot);
        if (obj_index < 0) {
            free_io_request(slot);
            *result = (u64) -1;
            return true;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, 0);
        if (handle_idx < 0) {
            free_object(obj_index);
            free_io_request(slot);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 19) {
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
        if (g_objects[obj_index].type != OBJ_IO_REQUEST) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        if (!g_io_requests[slot].is_write) {
            *result = (u64) -1;  // wrong wait syscall for a read-issued handle
            return true;
        }
        io_request_wait(slot);
        int io_result = g_io_requests[slot].result;
        free_io_request(slot);
        free_object(obj_index);
        free_handle(caller_process, handle_idx);
        *result = (u64) io_result;
        return true;
    }
    return false;
}
