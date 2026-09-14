#include "wifi.h"
#include "../../sched/task.h"
#include "../../../proc/ipc/object/object.h"
#include "../../../proc/ipc/wifi_request/wifi_request.h"
#include "../../net/rtw89/wifi_manager.h"

bool syscall_wifi(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    (void) a2;
    (void) a3;
    if (num == 101) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int slot = alloc_wifi_scan_request();
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = alloc_object(OBJ_WIFI_REQUEST, slot);
        if (obj_index < 0) {
            free_wifi_request(slot);
            *result = (u64) -1;
            return true;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, 0);
        if (handle_idx < 0) {
            free_object(obj_index);
            free_wifi_request(slot);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 102) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS
            || !g_handle_tables[caller_process][handle_idx].used) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_WIFI_REQUEST) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        wifi_request_wait(slot);
        wifi_network* out_buf = (wifi_network*) a2;
        u32 max_count = (u32) a3;
        int n = g_wifi_requests[slot].scan_count;
        if (n > (int) max_count) {
            n = (int) max_count;
        }
        int i = 0;
        while (i < n) {
            out_buf[i] = g_wifi_requests[slot].scan_results[i];
            i = i + 1;
        }
        free_wifi_request(slot);
        free_object(obj_index);
        free_handle(caller_process, handle_idx);
        *result = (u64) n;
        return true;
    }
    if (num == 103) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        const char* ssid = (const char*) a1;
        const char* passphrase = (const char*) a2;
        u32 passphrase_len = (u32) a3;
        int slot = alloc_wifi_connect_request(ssid, passphrase, passphrase_len);
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = alloc_object(OBJ_WIFI_REQUEST, slot);
        if (obj_index < 0) {
            free_wifi_request(slot);
            *result = (u64) -1;
            return true;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, 0);
        if (handle_idx < 0) {
            free_object(obj_index);
            free_wifi_request(slot);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 104) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS
            || !g_handle_tables[caller_process][handle_idx].used) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_WIFI_REQUEST) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        wifi_request_wait(slot);
        wifi_state state = g_wifi_requests[slot].connect_result;
        free_wifi_request(slot);
        free_object(obj_index);
        free_handle(caller_process, handle_idx);
        *result = (u64) state;
        return true;
    }
    if (num == 105) {
        char* ssid_out = (char*) a1;
        u32 max_len = (u32) a2;
        if (g_wifi_state == WIFI_STATE_CONNECTED && ssid_out != 0 && max_len > 0) {
            u32 i = 0;
            while (i < max_len - 1 && g_wifi_connected_ssid[i] != 0) {
                ssid_out[i] = g_wifi_connected_ssid[i];
                i = i + 1;
            }
            ssid_out[i] = 0;
        }
        *result = (u64) g_wifi_state;
        return true;
    }
    return false;
}
