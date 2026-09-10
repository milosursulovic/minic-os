#include "channel.h"
#include "../../sched/task.h"
#include "../../../proc/ipc/object/object.h"
#include "../../../proc/ipc/channel/channel.h"

bool syscall_channel(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    if (num == 7) {
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
        if (g_objects[obj_index].type != OBJ_CHANNEL) {
            *result = (u64) -1;
            return true;
        }
        int channel_index = g_objects[obj_index].data_index;
        bool ok = channel_send(channel_index, a2);
        if (!ok) {
            *result = (u64) -1;
            return true;
        }
        *result = 0;
        return true;
    }
    if (num == 8) {
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
        if (g_objects[obj_index].type != OBJ_CHANNEL) {
            *result = (u64) -1;
            return true;
        }
        int channel_index = g_objects[obj_index].data_index;
        *result = channel_receive(channel_index);
        return true;
    }
    if (num == 9) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int channel_index = (int) a1;
        if (channel_index < 0 || channel_index >= g_channel_count) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = alloc_object(OBJ_CHANNEL, channel_index);
        if (obj_index < 0) {
            *result = (u64) -1;
            return true;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, RIGHT_RECEIVE);
        if (handle_idx < 0) {
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 85) {
        // channel_send_msg - Faza I point 8 item 4. Same handle/rights
        // validation as syscall 7 (channel_send), plus a real payload
        // length cap. a2 is dereferenced directly as a user pointer, the
        // same no-copy_from_user convention every other ptr-taking
        // syscall in this kernel already uses (e.g. syscall 1/46).
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
        if (g_objects[obj_index].type != OBJ_CHANNEL) {
            *result = (u64) -1;
            return true;
        }
        u32 len = (u32) a3;
        if (len > CHANNEL_MSG_MAX) {
            *result = (u64) -1;
            return true;
        }
        int channel_index = g_objects[obj_index].data_index;
        bool ok = channel_send_msg(channel_index, (const void*) a2, len);
        *result = ok ? 0 : (u64) -1;
        return true;
    }
    if (num == 86) {
        // channel_receive_msg - Faza I point 8 item 4. Same handle/rights
        // validation as syscall 8 (channel_receive).
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
        if (g_objects[obj_index].type != OBJ_CHANNEL) {
            *result = (u64) -1;
            return true;
        }
        int channel_index = g_objects[obj_index].data_index;
        u32 max_len = (u32) a3;
        u32 got = channel_receive_msg(channel_index, (void*) a2, max_len);
        *result = (u64) got;
        return true;
    }
    return false;
}
