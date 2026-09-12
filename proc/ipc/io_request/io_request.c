// Backs syscalls 16-19 (async file read/write, issue+wait).

#include "io_request.h"
#include "../../../kernel/fs/vfs/vfs.h"
#include "../../../kernel/sched/task.h"

// Same disable_interrupts()/restore_interrupts() pattern
// kernel/sched/task.c's yield()/io_request_wait() use (own copy - no
// shared header, see task.c's own comment) - protects io_worker_entry()'s
// write to the same `done` flag io_request_wait()'s blocked/waiting_on
// pair watches.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

io_request g_io_requests[IO_REQUEST_SLOTS];

static int alloc_io_request_slot(const char* path) {
    int i = 0;
    while (i < IO_REQUEST_SLOTS) {
        if (!g_io_requests[i].used) {
            int j = 0;
            while (j < 63 && path[j] != 0) {
                g_io_requests[i].path[j] = path[j];
                j = j + 1;
            }
            g_io_requests[i].path[j] = 0;
            g_io_requests[i].used = true;
            g_io_requests[i].done = false;
            g_io_requests[i].result = -1;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

int alloc_io_request(const char* path) {
    int slot = alloc_io_request_slot(path);
    if (slot < 0) {
        return -1;
    }
    g_io_requests[slot].is_write = false;
    return slot;
}

int alloc_io_write_request(const char* path, u8* payload, u32 payload_len) {
    int slot = alloc_io_request_slot(path);
    if (slot < 0) {
        return -1;
    }
    g_io_requests[slot].is_write = true;
    u32 n = payload_len;
    if (n > IO_REQUEST_BUF_SIZE) {
        n = IO_REQUEST_BUF_SIZE;
    }
    u32 i = 0;
    while (i < n) {
        g_io_requests[slot].buffer[i] = payload[i];
        i = i + 1;
    }
    g_io_requests[slot].payload_len = n;
    return slot;
}

void free_io_request(int slot_index) {
    g_io_requests[slot_index].used = false;
}

void io_worker_entry(void) {
    for (;;) {
        int i = 0;
        while (i < IO_REQUEST_SLOTS) {
            if (g_io_requests[i].used && !g_io_requests[i].done) {
                if (g_io_requests[i].is_write) {
                    // Out of item 7's scope (Faza I point 5) - unchanged,
                    // fully-permissive async I/O, same as before.
                    bool ok = vfs_write(&g_io_requests[i].path[0], &g_io_requests[i].buffer[0], g_io_requests[i].payload_len, 0);
                    g_io_requests[i].result = ok ? (int) g_io_requests[i].payload_len : -1;
                } else {
                    int n = vfs_read(&g_io_requests[i].path[0], &g_io_requests[i].buffer[0], IO_REQUEST_BUF_SIZE, 0);
                    g_io_requests[i].result = n;
                }
                u64 saved_flags = disable_interrupts();
                g_io_requests[i].done = true;
                restore_interrupts(saved_flags);
            }
            i = i + 1;
        }
        yield();
    }
}
