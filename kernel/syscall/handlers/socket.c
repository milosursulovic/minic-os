#include "socket.h"
#include "../../sched/task.h"
#include "../../../proc/ipc/object/object.h"
#include "../../../proc/ipc/socket/socket.h"

bool syscall_socket(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    if (num == 59) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int slot = socket_create_listener((u16) a1);
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = alloc_object(OBJ_SOCKET, slot);
        if (obj_index < 0) {
            *result = (u64) -1;
            return true;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, RIGHT_SEND | RIGHT_RECEIVE);
        if (handle_idx < 0) {
            free_object(obj_index);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 60) {
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
        if (g_objects[obj_index].type != OBJ_SOCKET) {
            *result = (u64) -1;
            return true;
        }
        int socket_slot = g_objects[obj_index].data_index;
        u8 remote_ip[4];
        u16 remote_port;
        int accepted_slot = socket_accept(socket_slot, 3000, &remote_ip[0], &remote_port);
        if (accepted_slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int accepted_obj = alloc_object(OBJ_SOCKET, accepted_slot);
        if (accepted_obj < 0) {
            *result = (u64) -1;
            return true;
        }
        int accepted_handle = alloc_handle(caller_process, accepted_obj, RIGHT_SEND | RIGHT_RECEIVE);
        if (accepted_handle < 0) {
            free_object(accepted_obj);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) accepted_handle;
        return true;
    }
    if (num == 61) {
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
        if (g_objects[obj_index].type != OBJ_SOCKET) {
            *result = (u64) -1;
            return true;
        }
        int socket_slot = g_objects[obj_index].data_index;
        u8* data = (u8*) a2;
        u32 n = socket_send(socket_slot, data, (u16) a3);
        *result = (u64) n;
        return true;
    }
    if (num == 62) {
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
        if (g_objects[obj_index].type != OBJ_SOCKET) {
            *result = (u64) -1;
            return true;
        }
        int socket_slot = g_objects[obj_index].data_index;
        u8* buf = (u8*) a2;
        u32 n = socket_receive(socket_slot, buf, (u32) a3, 3000);
        *result = (u64) n;
        return true;
    }
    if (num == 63) {
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
        if (g_objects[obj_index].type != OBJ_SOCKET) {
            *result = (u64) -1;
            return true;
        }
        int socket_slot = g_objects[obj_index].data_index;
        socket_close(socket_slot);
        free_object(obj_index);
        free_handle(caller_process, handle_idx);
        *result = 0;
        return true;
    }
    return false;
}
