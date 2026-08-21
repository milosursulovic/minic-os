// Ring3 test program, compiled standalone (see ring3.ld) and loaded via
// spawn_process(). Exercises the File/Channel/Process/ProcessHandle API
// and a thin POSIX shim.
//
// _start must be at offset 0 of the loaded image - `__attribute__((section(".text.start")))`
// plus ring3.ld's ".text.start" force that regardless of gcc's own
// function ordering.

#include "../types.h"

#define RIGHT_QUERY 1

typedef struct {
    char* path;
} file;

typedef struct {
    int handle;
} channel;

typedef struct {
    char* path;
} process;

typedef struct {
    int handle;
} process_handle;

typedef struct {
    int id;
} window;

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

typedef struct {
    bool used;
    bool for_writing;
    char* path;
    u64 position;
    u64 length;
    u8 buffer[256];
} file_descriptor;

static u64 do_syscall(u64 num, u64 arg1, u64 arg2, u64 arg3) {
    u64 result;
    register u64 r_num __asm__("rax") = num;
    register u64 r_arg1 __asm__("rdi") = arg1;
    register u64 r_arg2 __asm__("rsi") = arg2;
    register u64 r_arg3 __asm__("rdx") = arg3;
    __asm__ volatile("int $0x80"
                      : "+r"(r_num)
                      : "r"(r_arg1), "r"(r_arg2), "r"(r_arg3)
                      : "memory");
    result = r_num;
    return result;
}

static u64 file_write(file* self, char* buf, u64 len) {
    return do_syscall(5, (u64) self->path, (u64) buf, len);
}

static u64 file_read(file* self, char* buf, u64 max_len) {
    return do_syscall(4, (u64) self->path, (u64) buf, max_len);
}

static u8 g_read_buf[64];
static u8 g_async_buf[64];

static bool channel_open(channel* self, int channel_index) {
    u64 result = do_syscall(9, (u64) channel_index, 0, 0);
    if (result == (u64) -1) {
        return false;
    }
    self->handle = (int) result;
    return true;
}

static bool channel_send(channel* self, u64 value) {
    u64 result = do_syscall(7, (u64) self->handle, value, 0);
    return result != (u64) -1;
}

static u64 channel_receive(channel* self) {
    return do_syscall(8, (u64) self->handle, 0, 0);
}

static u64 process_spawn(process* self, u64 load_vaddr, u64 stack_vaddr) {
    return do_syscall(6, (u64) self->path, load_vaddr, stack_vaddr);
}

static bool process_handle_open(process_handle* self, int task_index, int requested_rights) {
    u64 result = do_syscall(10, (u64) task_index, (u64) requested_rights, 0);
    if (result == (u64) -1) {
        return false;
    }
    self->handle = (int) result;
    return true;
}

static u64 process_handle_query(process_handle* self) {
    return do_syscall(3, (u64) self->handle, 0, 0);
}

static bool window_create(window* self, i32 x, i32 y, u32 width, u32 height,
                           u32 body_color, u32 title_color) {
    window_create_args args;
    args.x = x;
    args.y = y;
    args.width = width;
    args.height = height;
    args.body_color = body_color;
    args.title_color = title_color;
    u64 result = do_syscall(26, (u64) &args, 0, 0);
    if (result == (u64) -1) {
        return false;
    }
    self->id = (int) result;
    return true;
}

static bool window_move(window* self, i32 x, i32 y) {
    u64 result = do_syscall(27, (u64) self->id, (u64) x, (u64) y);
    return result != (u64) -1;
}

static bool window_raise(window* self) {
    u64 result = do_syscall(28, (u64) self->id, 0, 0);
    return result != (u64) -1;
}

static bool window_close(window* self) {
    u64 result = do_syscall(29, (u64) self->id, 0, 0);
    return result != (u64) -1;
}

static bool window_fill_rect(window* self, u32 x, u32 y, u32 width, u32 height, u32 color) {
    window_fill_rect_args args;
    args.id = self->id;
    args.x = x;
    args.y = y;
    args.width = width;
    args.height = height;
    args.color = color;
    u64 result = do_syscall(30, (u64) &args, 0, 0);
    return result != (u64) -1;
}

// mode 0 = read, mode 1 = write (flushed as a new file on close).
static file_descriptor g_fd_table[4];

static int posix_open(char* path, int mode) {
    int fd = -1;
    int i = 0;
    while (i < 4) {
        if (!g_fd_table[i].used) {
            fd = i;
            break;
        }
        i = i + 1;
    }
    if (fd < 0) {
        return -1;
    }
    g_fd_table[fd].used = true;
    g_fd_table[fd].for_writing = (mode == 1);
    g_fd_table[fd].path = path;
    g_fd_table[fd].position = 0;
    if (mode == 1) {
        g_fd_table[fd].length = 0;
    } else {
        file f;
        f.path = path;
        u64 n = file_read(&f, (char*) &g_fd_table[fd].buffer[0], 255);
        if (n == (u64) -1) {
            g_fd_table[fd].used = false;
            return -1;
        }
        g_fd_table[fd].length = n;
    }
    return fd;
}

static int posix_read(int fd, char* buf, int len) {
    if (fd < 0 || fd >= 4 || !g_fd_table[fd].used) {
        return -1;
    }
    u64 remaining = g_fd_table[fd].length - g_fd_table[fd].position;
    u64 n = (u64) len;
    if (n > remaining) {
        n = remaining;
    }
    u64 i = 0;
    while (i < n) {
        buf[i] = (char) g_fd_table[fd].buffer[g_fd_table[fd].position + i];
        i = i + 1;
    }
    g_fd_table[fd].position = g_fd_table[fd].position + n;
    return (int) n;
}

static int posix_write(int fd, char* buf, int len) {
    if (fd < 0 || fd >= 4 || !g_fd_table[fd].used) {
        return -1;
    }
    int i = 0;
    while (i < len && g_fd_table[fd].length < 256) {
        g_fd_table[fd].buffer[g_fd_table[fd].length] = (u8) buf[i];
        g_fd_table[fd].length = g_fd_table[fd].length + 1;
        i = i + 1;
    }
    return i;
}

static int posix_close(int fd) {
    if (fd < 0 || fd >= 4 || !g_fd_table[fd].used) {
        return -1;
    }
    int result = 0;
    if (g_fd_table[fd].for_writing) {
        file f;
        f.path = g_fd_table[fd].path;
        u64 written = file_write(&f, (char*) &g_fd_table[fd].buffer[0], g_fd_table[fd].length);
        if (written == (u64) -1) {
            result = -1;
        }
    }
    g_fd_table[fd].used = false;
    return result;
}

__attribute__((section(".text.start")))
void _start(void) {
    do_syscall(1, (u64) "hello from a LOADED process! 0x", 0xC0FFEE, 0);
    do_syscall(1, (u64) "hello from a LOADED process! 0x", 0xC0FFEE, 0);

    // handle 0 = myself.
    u64 self_task_index = do_syscall(3, 0, 0, 0);
    do_syscall(1, (u64) "handle 0 (self) -> task_index 0x", self_task_index, 0);

    // handle 99 was never allocated - expect the -1 sentinel.
    u64 bad_result = do_syscall(3, 99, 0, 0);
    do_syscall(1, (u64) "handle 99 (invalid) -> 0x", bad_result, 0);

    file msg_file;
    msg_file.path = "/system/ring3msg.txt";
    u64 written = file_write(&msg_file, "hello from ring3, via a real File.write() method call!", 54);
    do_syscall(1, (u64) "File.write() wrote 0x", written, 0);

    u64 read_back = file_read(&msg_file, (char*) &g_read_buf[0], 63);
    g_read_buf[read_back] = 0;
    do_syscall(1, (u64) "File.read() got back 0x", read_back, 0);
    do_syscall(1, (u64) &g_read_buf[0], 0, 0);

    int wfd = posix_open("/system/posix.txt", 1);
    posix_write(wfd, "POSIX ", 6);
    posix_write(wfd, "shim works!", 11);
    posix_close(wfd);

    int rfd = posix_open("/system/posix.txt", 0);
    char posix_buf1[8];
    int n1 = posix_read(rfd, &posix_buf1[0], 6);
    posix_buf1[n1] = 0;
    char posix_buf2[16];
    int n2 = posix_read(rfd, &posix_buf2[0], 11);
    posix_buf2[n2] = 0;
    posix_close(rfd);

    do_syscall(1, (u64) "POSIX read() 1: ", 0, 0);
    do_syscall(1, (u64) &posix_buf1[0], 0, 0);
    do_syscall(1, (u64) "POSIX read() 2: ", 0, 0);
    do_syscall(1, (u64) &posix_buf2[0], 0, 0);

    // channel index 1 - matches kmain.c's create_channel() order.
    // Unauthorized send() below is expected to fail (RIGHT_RECEIVE only).
    channel spawn_trigger;
    bool opened = channel_open(&spawn_trigger, 1);
    do_syscall(1, (u64) "Channel.open() ok=0x", (u64) opened, 0);

    bool unauthorized_send_ok = channel_send(&spawn_trigger, 0xDEADBEEF);
    do_syscall(1, (u64) "unauthorized Channel.send() succeeded=0x", (u64) unauthorized_send_ok, 0);

    u64 trigger_value = channel_receive(&spawn_trigger);
    do_syscall(1, (u64) "Channel.receive() got trigger 0x", trigger_value, 0);

    // trigger 2 (ring3fault): forbidden write to kernel space - must page fault.
    if (trigger_value == 2) {
        do_syscall(1, (u64) "attempting forbidden ring3 write to 0x", 0x100000, 0);
        u64* forbidden = (u64*) 0x100000;
        *forbidden = 0xDEADBEEF;
        do_syscall(1, (u64) "forbidden write succeeded (BUG!) at 0x", 0x100000, 0);
    } else if (trigger_value == 3) {
        // trigger 3 (ring3nx): execute a byte on the user stack - must NX fault.
        do_syscall(1, (u64) "attempting to execute ring3 stack byte at 0x", 0x80020000, 0);
        u8* stack_code = (u8*) 0x80020000;
        *stack_code = 0xC3;
        void (*fn)(void) = (void (*)(void)) stack_code;
        fn();
        do_syscall(1, (u64) "stack execution succeeded (BUG!) at 0x", 0x80020000, 0);
    } else if (trigger_value == 4) {
        // trigger 4 (ring3reg): register testprog.bin at runtime, spawn it by index.
        u64 service_index = do_syscall(14, (u64) "/system/testprog.bin", 0, 0);
        do_syscall(1, (u64) "register_service() got index 0x", service_index, 0);

        u64 spawned_task_index = do_syscall(11, service_index, 0, 0);
        do_syscall(1, (u64) "spawn_builtin(registered) launched task_index 0x", spawned_task_index, 0);
    } else if (trigger_value == 5) {
        // trigger 5 (ring3unreg): register, unregister, confirm the slot is
        // freed (spawn_builtin fails), then register again - real reuse.
        u64 first_index = do_syscall(14, (u64) "/system/testprog.bin", 0, 0);
        do_syscall(1, (u64) "register_service() got index 0x", first_index, 0);

        u64 unreg_result = do_syscall(15, first_index, 0, 0);
        do_syscall(1, (u64) "unregister_service() result 0x", unreg_result, 0);

        u64 failed_spawn = do_syscall(11, first_index, 0, 0);
        do_syscall(1, (u64) "spawn_builtin(unregistered) got 0x", failed_spawn, 0);

        u64 second_index = do_syscall(14, (u64) "/system/testprog.bin", 0, 0);
        do_syscall(1, (u64) "register_service() again got index 0x", second_index, 0);

        u64 reused_task_index = do_syscall(11, second_index, 0, 0);
        do_syscall(1, (u64) "spawn_builtin(re-registered) launched task_index 0x", reused_task_index, 0);
    } else if (trigger_value == 6) {
        // trigger 6 (ring3async): async read, do work before waiting.
        u64 async_handle = do_syscall(16, (u64) "/system/ring3msg.txt", 0, 0);
        do_syscall(1, (u64) "file_read_async() got handle 0x", async_handle, 0);

        int i = 0;
        while (i < 5) {
            do_syscall(1, (u64) "doing other work, iteration 0x", (u64) i, 0);
            i = i + 1;
        }

        u64 async_result = do_syscall(17, async_handle, (u64) &g_async_buf[0], 63);
        g_async_buf[async_result] = 0;
        do_syscall(1, (u64) "file_read_wait() got back 0x", async_result, 0);
        do_syscall(1, (u64) &g_async_buf[0], 0, 0);
    } else if (trigger_value == 7) {
        // trigger 7 (ring3asyncwrite): async write, then verify via sync read.
        char* write_payload = "async write via a real worker task, not a stub!";
        u64 write_handle = do_syscall(18, (u64) "/system/ring3asyncmsg.txt", (u64) write_payload, 47);
        do_syscall(1, (u64) "file_write_async() got handle 0x", write_handle, 0);

        int j = 0;
        while (j < 5) {
            do_syscall(1, (u64) "doing other work, iteration 0x", (u64) j, 0);
            j = j + 1;
        }

        u64 write_result = do_syscall(19, write_handle, 0, 0);
        do_syscall(1, (u64) "file_write_wait() got back 0x", write_result, 0);

        file async_written_file;
        async_written_file.path = "/system/ring3asyncmsg.txt";
        u64 verify_read = file_read(&async_written_file, (char*) &g_async_buf[0], 63);
        g_async_buf[verify_read] = 0;
        do_syscall(1, (u64) "verify File.read() got back 0x", verify_read, 0);
        do_syscall(1, (u64) &g_async_buf[0], 0, 0);
    } else if (trigger_value == 8) {
        // trigger 8 (ring3asyncping): async ping to the gateway.
        u64 gateway_ip = 0x0A000202;  // 10.0.2.2
        u64 ping_handle = do_syscall(20, gateway_ip, 0, 0);
        do_syscall(1, (u64) "net_ping_async() got handle 0x", ping_handle, 0);

        int k = 0;
        while (k < 5) {
            do_syscall(1, (u64) "doing other work, iteration 0x", (u64) k, 0);
            k = k + 1;
        }

        u64 ping_result = do_syscall(21, ping_handle, 0, 0);
        do_syscall(1, (u64) "net_ping_wait() got ok=0x", ping_result, 0);
    } else if (trigger_value == 9) {
        // trigger 9 (ring3asyncdns): async DNS resolve.
        u64 dns_handle = do_syscall(22, (u64) "example.com", 0, 0);
        do_syscall(1, (u64) "net_dns_async() got handle 0x", dns_handle, 0);

        int m = 0;
        while (m < 5) {
            do_syscall(1, (u64) "doing other work, iteration 0x", (u64) m, 0);
            m = m + 1;
        }

        u8 resolved_ip[4];
        u64 dns_result = do_syscall(23, dns_handle, (u64) &resolved_ip[0], 0);
        do_syscall(1, (u64) "net_dns_wait() got ok=0x", dns_result, 0);
        if (dns_result != 0) {
            u64 packed_ip = ((u64) resolved_ip[0] << 24) | ((u64) resolved_ip[1] << 16)
                | ((u64) resolved_ip[2] << 8) | (u64) resolved_ip[3];
            do_syscall(1, (u64) "resolved IP (packed) 0x", packed_ip, 0);
        }
    } else if (trigger_value == 10) {
        // trigger 10 (ring3asynctcp): chains async DNS resolve into async TCP fetch.
        u64 dns_handle = do_syscall(22, (u64) "example.com", 0, 0);
        do_syscall(1, (u64) "net_dns_async() got handle 0x", dns_handle, 0);
        u8 resolved_ip[4];
        u64 dns_result = do_syscall(23, dns_handle, (u64) &resolved_ip[0], 0);
        do_syscall(1, (u64) "net_dns_wait() got ok=0x", dns_result, 0);

        if (dns_result != 0) {
            u64 packed_ip = ((u64) resolved_ip[0] << 24) | ((u64) resolved_ip[1] << 16)
                | ((u64) resolved_ip[2] << 8) | (u64) resolved_ip[3];
            u64 packed_ip_and_port = (packed_ip << 16) | 80;
            char* request = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
            u64 tcp_handle = do_syscall(24, packed_ip_and_port, (u64) request, 58);
            do_syscall(1, (u64) "net_tcp_fetch_async() got handle 0x", tcp_handle, 0);

            int n = 0;
            while (n < 5) {
                do_syscall(1, (u64) "doing other work, iteration 0x", (u64) n, 0);
                n = n + 1;
            }

            u64 tcp_result = do_syscall(25, tcp_handle, (u64) &g_async_buf[0], 63);
            do_syscall(1, (u64) "net_tcp_fetch_wait() got response_len=0x", tcp_result, 0);
            if (tcp_result != (u64) -1) {
                g_async_buf[tcp_result] = 0;
                do_syscall(1, (u64) &g_async_buf[0], 0, 0);
            }
        }
    } else if (trigger_value == 11) {
        // trigger 11 (ring3win): create/raise/move/close real windows via syscalls.
        window a;
        bool created_a = window_create(&a, 50, 50, 200, 150, 0x00FF0000, 0x00800000);
        do_syscall(1, (u64) "window_create(a) ok=0x", (u64) created_a, 0);
        do_syscall(1, (u64) "window a.id=0x", (u64) a.id, 0);

        window b;
        bool created_b = window_create(&b, 150, 120, 200, 150, 0x0000FF00, 0x00008000);
        do_syscall(1, (u64) "window_create(b) ok=0x", (u64) created_b, 0);
        do_syscall(1, (u64) "window b.id=0x", (u64) b.id, 0);

        window c;
        bool created_c = window_create(&c, 600, 50, 100, 100, 0x000000FF, 0x00000080);
        do_syscall(1, (u64) "window_create(c) ok=0x", (u64) created_c, 0);
        do_syscall(1, (u64) "window c.id=0x", (u64) c.id, 0);

        bool raised = window_raise(&a);
        do_syscall(1, (u64) "window_raise(a) ok=0x", (u64) raised, 0);

        bool moved = window_move(&c, 600, 400);
        do_syscall(1, (u64) "window_move(c) ok=0x", (u64) moved, 0);

        bool closed = window_close(&c);
        do_syscall(1, (u64) "window_close(c) ok=0x", (u64) closed, 0);

        // Real app-drawn content, not a flat placeholder - a base fill plus
        // an accent rect, left open (unlike c) so its result is checkable.
        window d;
        bool created_d = window_create(&d, 50, 300, 150, 150, 0x00444444, 0x00222222);
        do_syscall(1, (u64) "window_create(d) ok=0x", (u64) created_d, 0);
        do_syscall(1, (u64) "window d.id=0x", (u64) d.id, 0);

        bool filled_base = window_fill_rect(&d, 0, 0, 150, 130, 0x00333333);
        do_syscall(1, (u64) "window_fill_rect(d, base) ok=0x", (u64) filled_base, 0);
        bool filled_accent = window_fill_rect(&d, 20, 20, 50, 50, 0x00FFFF00);
        do_syscall(1, (u64) "window_fill_rect(d, accent) ok=0x", (u64) filled_accent, 0);
    } else {
        process child_image;
        child_image.path = "/system/testprog.bin";
        u64 child_task_index = process_spawn(&child_image, 0x80000000, 0x80020000);
        do_syscall(1, (u64) "Process.spawn() launched task_index 0x", child_task_index, 0);

        if (child_task_index != (u64) -1) {
            process_handle no_rights;
            bool opened_no_rights = process_handle_open(&no_rights, (int) child_task_index, 0);
            do_syscall(1, (u64) "ProcessHandle.open(rights=0) ok=0x", (u64) opened_no_rights, 0);
            u64 unauthorized_query = process_handle_query(&no_rights);
            do_syscall(1, (u64) "unauthorized ProcessHandle.query() got 0x", unauthorized_query, 0);

            process_handle query_rights;
            bool opened_query = process_handle_open(&query_rights, (int) child_task_index, RIGHT_QUERY);
            do_syscall(1, (u64) "ProcessHandle.open(RIGHT_QUERY) ok=0x", (u64) opened_query, 0);
            u64 authorized_query = process_handle_query(&query_rights);
            do_syscall(1, (u64) "authorized ProcessHandle.query() got task_index 0x", authorized_query, 0);
        }
    }

    for (;;) {
    }
}
