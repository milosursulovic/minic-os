// Async file reads: file_read_async() (syscall 16) issues a request and
// returns immediately; a dedicated kernel worker task performs the real
// vfs_read() concurrently; file_read_wait() (syscall 17) blocks the
// caller only once it actually needs the result.

#include "io_request.h"
#include "../disk/vfs.h"
#include "../sched/task.h"

io_request g_io_requests[IO_REQUEST_SLOTS];

int alloc_io_request(const char* path) {
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

void free_io_request(int slot_index) {
    g_io_requests[slot_index].used = false;
}

void io_worker_entry(void) {
    for (;;) {
        int i = 0;
        while (i < IO_REQUEST_SLOTS) {
            if (g_io_requests[i].used && !g_io_requests[i].done) {
                int n = vfs_read(&g_io_requests[i].path[0], &g_io_requests[i].buffer[0], IO_REQUEST_BUF_SIZE);
                g_io_requests[i].result = n;
                g_io_requests[i].done = true;
            }
            i = i + 1;
        }
        yield();
    }
}
