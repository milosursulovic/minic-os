#include "net_request.h"
#include "../../sched/task.h"
#include "../../../proc/ipc/object/object.h"
#include "../../../proc/ipc/net_request/net_request.h"
#include "../../../proc/ipc/net_tcp_request/net_tcp_request.h"

bool syscall_net_request(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    if (num == 20) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        u32 packed_ip = (u32) a1;
        u8 target_ip[4];
        target_ip[0] = (u8) (packed_ip >> 24);
        target_ip[1] = (u8) (packed_ip >> 16);
        target_ip[2] = (u8) (packed_ip >> 8);
        target_ip[3] = (u8) packed_ip;
        int slot = alloc_net_ping_request(&target_ip[0]);
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = alloc_object(OBJ_NET_PING_REQUEST, slot);
        if (obj_index < 0) {
            free_net_ping_request(slot);
            *result = (u64) -1;
            return true;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, 0);
        if (handle_idx < 0) {
            free_object(obj_index);
            free_net_ping_request(slot);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 21) {
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
        if (g_objects[obj_index].type != OBJ_NET_PING_REQUEST) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        if (g_net_ping_requests[slot].is_dns) {
            *result = (u64) -1;  // wrong wait syscall for a DNS-issued handle
            return true;
        }
        net_ping_request_wait(slot);
        bool ok = g_net_ping_requests[slot].ok;
        free_net_ping_request(slot);
        free_object(obj_index);
        free_handle(caller_process, handle_idx);
        *result = (u64) ok;
        return true;
    }
    if (num == 22) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        char* hostname = (char*) a1;
        int slot = alloc_net_dns_request(hostname);
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = alloc_object(OBJ_NET_PING_REQUEST, slot);
        if (obj_index < 0) {
            free_net_ping_request(slot);
            *result = (u64) -1;
            return true;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, 0);
        if (handle_idx < 0) {
            free_object(obj_index);
            free_net_ping_request(slot);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 23) {
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
        if (g_objects[obj_index].type != OBJ_NET_PING_REQUEST) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        if (!g_net_ping_requests[slot].is_dns) {
            *result = (u64) -1;  // wrong wait syscall for a ping-issued handle
            return true;
        }
        net_ping_request_wait(slot);
        bool ok = g_net_ping_requests[slot].ok;
        if (ok) {
            u8* ip_out = (u8*) a2;
            int i = 0;
            while (i < 4) {
                ip_out[i] = g_net_ping_requests[slot].resolved_ip[i];
                i = i + 1;
            }
        }
        free_net_ping_request(slot);
        free_object(obj_index);
        free_handle(caller_process, handle_idx);
        *result = (u64) ok;
        return true;
    }
    if (num == 24) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        u64 packed = a1;
        u16 target_port = (u16) (packed & 0xFFFF);
        u32 packed_ip = (u32) (packed >> 16);
        u8 target_ip[4];
        target_ip[0] = (u8) (packed_ip >> 24);
        target_ip[1] = (u8) (packed_ip >> 16);
        target_ip[2] = (u8) (packed_ip >> 8);
        target_ip[3] = (u8) packed_ip;
        char* payload = (char*) a2;
        u16 payload_len = (u16) a3;
        int slot = alloc_net_tcp_request(&target_ip[0], target_port, payload, payload_len);
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = alloc_object(OBJ_NET_TCP_REQUEST, slot);
        if (obj_index < 0) {
            free_net_tcp_request(slot);
            *result = (u64) -1;
            return true;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, 0);
        if (handle_idx < 0) {
            free_object(obj_index);
            free_net_tcp_request(slot);
            *result = (u64) -1;
            return true;
        }
        *result = (u64) handle_idx;
        return true;
    }
    if (num == 25) {
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
        if (g_objects[obj_index].type != OBJ_NET_TCP_REQUEST) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        net_tcp_request_wait(slot);
        bool ok = g_net_tcp_requests[slot].ok;
        u32 response_len = g_net_tcp_requests[slot].response_len;
        if (response_len > 0) {
            u8* buf = (u8*) a2;
            u32 max_len = (u32) a3;
            u32 n = response_len;
            if (n > max_len) {
                n = max_len;
            }
            u32 i = 0;
            while (i < n) {
                buf[i] = g_net_tcp_requests[slot].response[i];
                i = i + 1;
            }
        }
        free_net_tcp_request(slot);
        free_object(obj_index);
        free_handle(caller_process, handle_idx);
        if (!ok) {
            *result = (u64) -1;
            return true;
        }
        *result = (u64) response_len;
        return true;
    }
    return false;
}
