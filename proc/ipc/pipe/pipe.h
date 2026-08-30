#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// A real byte-stream FIFO, unlike proc/ipc/channel/channel.h's single-
// slot u64 mailbox - matches roadmap point 8's own "Process A --stream-->
// Process B" diagram. Non-blocking (real partial-read/partial-write, 0
// on an empty read - same cooperative-polling style as io_request.h),
// backs syscalls 52-54.
#define PIPE_SLOTS 4
#define PIPE_BUF_SIZE 256

typedef struct {
    bool used;
    u8 buffer[PIPE_BUF_SIZE];
    u32 read_pos;
    u32 write_pos;
    u32 count;  // bytes currently buffered
} pipe_buffer;

extern pipe_buffer g_pipes[PIPE_SLOTS];

int alloc_pipe(void);
// Writes up to (PIPE_BUF_SIZE - count) bytes - real partial-write
// semantics if the pipe is nearly full, not silently dropping the rest.
// Returns the real byte count written.
u32 pipe_write(int index, const u8* data, u32 len);
// Reads up to min(max_len, count) bytes. Returns the real byte count
// read - 0 if empty, never blocks.
u32 pipe_read(int index, u8* out, u32 max_len);

#pragma GCC visibility pop
