#include "pipe.h"

pipe_buffer g_pipes[PIPE_SLOTS];

int alloc_pipe(void) {
    int i = 0;
    while (i < PIPE_SLOTS) {
        if (!g_pipes[i].used) {
            g_pipes[i].used = true;
            g_pipes[i].read_pos = 0;
            g_pipes[i].write_pos = 0;
            g_pipes[i].count = 0;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

u32 pipe_write(int index, const u8* data, u32 len) {
    pipe_buffer* p = &g_pipes[index];
    u32 space = PIPE_BUF_SIZE - p->count;
    u32 n = len < space ? len : space;
    u32 i = 0;
    while (i < n) {
        p->buffer[p->write_pos] = data[i];
        p->write_pos = (p->write_pos + 1) % PIPE_BUF_SIZE;
        i = i + 1;
    }
    p->count = p->count + n;
    return n;
}

u32 pipe_read(int index, u8* out, u32 max_len) {
    pipe_buffer* p = &g_pipes[index];
    u32 n = max_len < p->count ? max_len : p->count;
    u32 i = 0;
    while (i < n) {
        out[i] = p->buffer[p->read_pos];
        p->read_pos = (p->read_pos + 1) % PIPE_BUF_SIZE;
        i = i + 1;
    }
    p->count = p->count - n;
    return n;
}
