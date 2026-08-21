// int 0x80 syscall gate. rax = number in, return value out; rdi/rsi/rdx
// = up to 3 args.
//
// 1 print, 3 query handle, 4/5 vfs_read/write, 6 spawn (VFS path),
// 7/8/9 channel send/receive/open, 10 open_process, 11 spawn_builtin
// (registry index, not a raw pointer), 12 process_exit, 13 handle_close,
// 14/15 register/unregister_service, 16/17 file_read async/wait,
// 18/19 file_write async/wait, 20/21 net_ping async/wait, 22/23
// net_dns async/wait, 24/25 net_tcp_fetch async/wait, 26 window_create
// (pointer to a packed args struct - too many fields for 3 registers),
// 27/28/29 window_move/raise/close, 30 window_fill_rect (same
// pointer-to-struct reasoning as 26).

#include "syscall.h"
#include "../drivers/io.h"
#include "../lib/strings.h"
#include "../sched/task.h"
#include "../proc/process.h"
#include "../proc/object.h"
#include "../proc/channel.h"
#include "../proc/io_request.h"
#include "../proc/net_request.h"
#include "../proc/net_tcp_request.h"
#include "../disk/vfs.h"
#include "../mm/paging.h"
#include "../mm/frames.h"
#include "../drivers/vbe.h"
#include "../gfx/window.h"

typedef struct __attribute__((packed)) {
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    u32 body_color;
    u32 title_color;
} window_create_args;

typedef struct __attribute__((packed)) {
    int id;
    u32 x;
    u32 y;
    u32 width;
    u32 height;
    u32 color;
} window_fill_rect_args;

#pragma GCC visibility push(hidden)
extern u8 g_hello_service_prog_start;
extern u8 g_hello_service_prog_end;
#pragma GCC visibility pop

// Index 0 is the one fixed compile-time entry; indices 1+ map to
// runtime-registered slots (register_service, syscall 14) - the
// registry isn't only the compile-time table anymore.
#define REGISTERED_SERVICE_SLOTS 4
#define REGISTERED_SERVICE_MAX_BYTES 16384

static u8 g_registered_service_buf[REGISTERED_SERVICE_SLOTS][REGISTERED_SERVICE_MAX_BYTES];
static u32 g_registered_service_len[REGISTERED_SERVICE_SLOTS];
static bool g_registered_service_used[REGISTERED_SERVICE_SLOTS];

// Not a static array of &symbol pointers - that needs an absolute 64-bit
// relocation the ELF32 build container can't represent.
static bool builtin_program_bounds(int index, u8** start_out, u8** end_out) {
    if (index == 0) {
        *start_out = &g_hello_service_prog_start;
        *end_out = &g_hello_service_prog_end;
        return true;
    }
    int slot = index - 1;
    if (slot >= 0 && slot < REGISTERED_SERVICE_SLOTS && g_registered_service_used[slot]) {
        *start_out = &g_registered_service_buf[slot][0];
        *end_out = &g_registered_service_buf[slot][g_registered_service_len[slot]];
        return true;
    }
    return false;
}

// Safe to reuse across processes - each gets its own cloned address space.
static const u64 BUILTIN_LOAD_VADDR = 0x80000000;
static const u64 BUILTIN_STACK_VADDR = 0x80020000;

u64 syscall_dispatch(u64 num, u64 a1, u64 a2, u64 a3) {
    if (num == 1) {
        char* s = (char*) a1;
        serial_print(s);
        vga_print(s);
        print_hex(a2);
        serial_print("\n");
        return 0;
    }
    if (num == 3) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            return (u64) -1;
        }
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_QUERY) == 0) {
            return (u64) -1;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type == OBJ_PROCESS) {
            int proc_idx = g_objects[obj_index].data_index;
            if (!g_processes[proc_idx].used) {
                return (u64) -1;  // exited
            }
            return (u64) g_processes[proc_idx].task_index;
        }
        return (u64) -1;
    }
    if (num == 4) {
        char* path = (char*) a1;
        u8* buf = (u8*) a2;
        int n = vfs_read(path, buf, (u32) a3);
        if (n < 0) {
            return (u64) -1;
        }
        return (u64) n;
    }
    if (num == 5) {
        char* path = (char*) a1;
        u8* buf = (u8*) a2;
        bool ok = vfs_write(path, buf, (u32) a3);
        if (!ok) {
            return (u64) -1;
        }
        return a3;
    }
    if (num == 6) {
        char* path = (char*) a1;
        int proc_index = spawn_process_from_path(path, a2, a3);
        if (proc_index < 0) {
            return (u64) -1;
        }
        return (u64) g_processes[proc_index].task_index;
    }
    if (num == 7) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            return (u64) -1;
        }
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_SEND) == 0) {
            return (u64) -1;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_CHANNEL) {
            return (u64) -1;
        }
        int channel_index = g_objects[obj_index].data_index;
        bool ok = channel_send(channel_index, a2);
        if (!ok) {
            return (u64) -1;
        }
        return 0;
    }
    if (num == 8) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            return (u64) -1;
        }
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_RECEIVE) == 0) {
            return (u64) -1;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_CHANNEL) {
            return (u64) -1;
        }
        int channel_index = g_objects[obj_index].data_index;
        return channel_receive(channel_index);
    }
    if (num == 9) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int channel_index = (int) a1;
        if (channel_index < 0 || channel_index >= g_channel_count) {
            return (u64) -1;
        }
        int obj_index = alloc_object(OBJ_CHANNEL, channel_index);
        if (obj_index < 0) {
            return (u64) -1;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, RIGHT_RECEIVE);
        if (handle_idx < 0) {
            return (u64) -1;
        }
        return (u64) handle_idx;
    }
    if (num == 10) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int target_task = (int) a1;
        if (target_task < 0 || target_task >= g_task_count) {
            return (u64) -1;
        }
        int target_process = g_tasks[target_task].process_index;
        if (target_process < 0) {
            return (u64) -1;
        }
        int obj_index = alloc_object(OBJ_PROCESS, target_process);
        if (obj_index < 0) {
            return (u64) -1;
        }
        int granted_rights = ((int) a2) & RIGHT_QUERY;
        int handle_idx = alloc_handle(caller_process, obj_index, granted_rights);
        if (handle_idx < 0) {
            return (u64) -1;
        }
        return (u64) handle_idx;
    }
    if (num == 11) {
        u8* start;
        u8* end;
        if (!builtin_program_bounds((int) a1, &start, &end)) {
            return (u64) -1;
        }
        int proc_index = spawn_process(start, end, BUILTIN_LOAD_VADDR, BUILTIN_STACK_VADDR);
        if (proc_index < 0) {
            return (u64) -1;
        }
        return (u64) g_processes[proc_index].task_index;
    }
    if (num == 12) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process >= 0) {
            g_processes[caller_process].used = false;
            free_address_space(g_processes[caller_process].cr3);
            int h = 0;
            while (h < HANDLES_PER_PROCESS) {
                if (g_handle_tables[caller_process][h].used) {
                    free_object(g_handle_tables[caller_process][h].object_index);
                    free_handle(caller_process, h);
                }
                h = h + 1;
            }
        }
        g_tasks[g_current_task].used = false;
        yield();
        return 0;  // never actually reached - yield() never switches back to an exited task
    }
    if (num == 13) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            return (u64) -1;
        }
        free_object(g_handle_tables[caller_process][handle_idx].object_index);
        free_handle(caller_process, handle_idx);
        return 0;
    }
    if (num == 14) {
        char* path = (char*) a1;
        int slot = -1;
        int i = 0;
        while (i < REGISTERED_SERVICE_SLOTS) {
            if (!g_registered_service_used[i]) {
                slot = i;
                break;
            }
            i = i + 1;
        }
        if (slot < 0) {
            return (u64) -1;
        }
        int n = vfs_read(path, &g_registered_service_buf[slot][0], REGISTERED_SERVICE_MAX_BYTES);
        if (n < 0) {
            return (u64) -1;
        }
        g_registered_service_used[slot] = true;
        g_registered_service_len[slot] = (u32) n;
        return (u64) (slot + 1);  // index 0 stays reserved for the compile-time entry
    }
    if (num == 15) {
        int index = (int) a1;
        int slot = index - 1;
        if (slot < 0 || slot >= REGISTERED_SERVICE_SLOTS) {
            return (u64) -1;
        }
        if (!g_registered_service_used[slot]) {
            return (u64) -1;
        }
        g_registered_service_used[slot] = false;
        g_registered_service_len[slot] = 0;
        return 0;
    }
    if (num == 16) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        char* path = (char*) a1;
        int slot = alloc_io_request(path);
        if (slot < 0) {
            return (u64) -1;
        }
        int obj_index = alloc_object(OBJ_IO_REQUEST, slot);
        if (obj_index < 0) {
            free_io_request(slot);
            return (u64) -1;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, 0);
        if (handle_idx < 0) {
            free_object(obj_index);
            free_io_request(slot);
            return (u64) -1;
        }
        return (u64) handle_idx;
    }
    if (num == 17) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            return (u64) -1;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_IO_REQUEST) {
            return (u64) -1;
        }
        int slot = g_objects[obj_index].data_index;
        if (g_io_requests[slot].is_write) {
            return (u64) -1;  // wrong wait syscall for a write-issued handle
        }
        io_request_wait(slot);
        u8* buf = (u8*) a2;
        u32 max_len = (u32) a3;
        int result = g_io_requests[slot].result;
        if (result > 0) {
            u32 n = (u32) result;
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
        return (u64) result;
    }
    if (num == 18) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        char* path = (char*) a1;
        u8* payload = (u8*) a2;
        u32 payload_len = (u32) a3;
        int slot = alloc_io_write_request(path, payload, payload_len);
        if (slot < 0) {
            return (u64) -1;
        }
        int obj_index = alloc_object(OBJ_IO_REQUEST, slot);
        if (obj_index < 0) {
            free_io_request(slot);
            return (u64) -1;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, 0);
        if (handle_idx < 0) {
            free_object(obj_index);
            free_io_request(slot);
            return (u64) -1;
        }
        return (u64) handle_idx;
    }
    if (num == 19) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            return (u64) -1;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_IO_REQUEST) {
            return (u64) -1;
        }
        int slot = g_objects[obj_index].data_index;
        if (!g_io_requests[slot].is_write) {
            return (u64) -1;  // wrong wait syscall for a read-issued handle
        }
        io_request_wait(slot);
        int result = g_io_requests[slot].result;
        free_io_request(slot);
        free_object(obj_index);
        free_handle(caller_process, handle_idx);
        return (u64) result;
    }
    if (num == 20) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        u32 packed_ip = (u32) a1;
        u8 target_ip[4];
        target_ip[0] = (u8) (packed_ip >> 24);
        target_ip[1] = (u8) (packed_ip >> 16);
        target_ip[2] = (u8) (packed_ip >> 8);
        target_ip[3] = (u8) packed_ip;
        int slot = alloc_net_ping_request(&target_ip[0]);
        if (slot < 0) {
            return (u64) -1;
        }
        int obj_index = alloc_object(OBJ_NET_PING_REQUEST, slot);
        if (obj_index < 0) {
            free_net_ping_request(slot);
            return (u64) -1;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, 0);
        if (handle_idx < 0) {
            free_object(obj_index);
            free_net_ping_request(slot);
            return (u64) -1;
        }
        return (u64) handle_idx;
    }
    if (num == 21) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            return (u64) -1;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_NET_PING_REQUEST) {
            return (u64) -1;
        }
        int slot = g_objects[obj_index].data_index;
        if (g_net_ping_requests[slot].is_dns) {
            return (u64) -1;  // wrong wait syscall for a DNS-issued handle
        }
        net_ping_request_wait(slot);
        bool ok = g_net_ping_requests[slot].ok;
        free_net_ping_request(slot);
        free_object(obj_index);
        free_handle(caller_process, handle_idx);
        return (u64) ok;
    }
    if (num == 22) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        char* hostname = (char*) a1;
        int slot = alloc_net_dns_request(hostname);
        if (slot < 0) {
            return (u64) -1;
        }
        int obj_index = alloc_object(OBJ_NET_PING_REQUEST, slot);
        if (obj_index < 0) {
            free_net_ping_request(slot);
            return (u64) -1;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, 0);
        if (handle_idx < 0) {
            free_object(obj_index);
            free_net_ping_request(slot);
            return (u64) -1;
        }
        return (u64) handle_idx;
    }
    if (num == 23) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            return (u64) -1;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_NET_PING_REQUEST) {
            return (u64) -1;
        }
        int slot = g_objects[obj_index].data_index;
        if (!g_net_ping_requests[slot].is_dns) {
            return (u64) -1;  // wrong wait syscall for a ping-issued handle
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
        return (u64) ok;
    }
    if (num == 24) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
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
            return (u64) -1;
        }
        int obj_index = alloc_object(OBJ_NET_TCP_REQUEST, slot);
        if (obj_index < 0) {
            free_net_tcp_request(slot);
            return (u64) -1;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, 0);
        if (handle_idx < 0) {
            free_object(obj_index);
            free_net_tcp_request(slot);
            return (u64) -1;
        }
        return (u64) handle_idx;
    }
    if (num == 25) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            return (u64) -1;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_NET_TCP_REQUEST) {
            return (u64) -1;
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
            return (u64) -1;
        }
        return (u64) response_len;
    }
    if (num == 26) {
        if (!g_fb_enabled) {
            vbe_init(800, 600);
        }
        window_create_args* args = (window_create_args*) a1;
        int id = window_create(args->x, args->y, args->width, args->height,
                                args->body_color, args->title_color);
        if (id < 0) {
            return (u64) -1;
        }
        compositor_redraw();
        return (u64) id;
    }
    if (num == 27) {
        bool ok = window_move((int) a1, (i32) a2, (i32) a3);
        if (!ok) {
            return (u64) -1;
        }
        compositor_redraw();
        return 0;
    }
    if (num == 28) {
        bool ok = window_raise((int) a1);
        if (!ok) {
            return (u64) -1;
        }
        compositor_redraw();
        return 0;
    }
    if (num == 29) {
        bool ok = window_close((int) a1);
        if (!ok) {
            return (u64) -1;
        }
        compositor_redraw();
        return 0;
    }
    if (num == 30) {
        window_fill_rect_args* args = (window_fill_rect_args*) a1;
        bool ok = window_fill_content_rect(args->id, args->x, args->y, args->width,
                                            args->height, args->color);
        if (!ok) {
            return (u64) -1;
        }
        compositor_redraw();
        return 0;
    }
    return (u64) -1;  // unknown syscall
}
