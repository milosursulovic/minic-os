#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

// Serviced by one worker task - "async" via cooperative multitasking, not DMA.
#define IO_REQUEST_SLOTS 4
#define IO_REQUEST_BUF_SIZE 512

typedef struct {
    bool used;
    bool done;
    bool is_write;
    char path[64];
    u8 buffer[IO_REQUEST_BUF_SIZE];
    u32 payload_len;  // for a write request: how many bytes of buffer to write
    int result;  // byte count, or -1 on failure - only meaningful once done
} io_request;

extern io_request g_io_requests[IO_REQUEST_SLOTS];

int alloc_io_request(const char* path);
int alloc_io_write_request(const char* path, u8* payload, u32 payload_len);
void free_io_request(int slot_index);
void io_worker_entry(void);

#pragma GCC visibility pop
