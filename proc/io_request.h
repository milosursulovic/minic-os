#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

// A single dedicated kernel worker task services these - the "async" part
// comes from cooperative multitasking, not hardware DMA/interrupts (the
// ATA driver is PIO-only). used = issued, done = worker has produced a
// result. The path is copied in at issue time since the worker task runs
// with the kernel's own CR3, which can't see a caller's private ring3
// image where a string literal path pointer usually lives.
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
// Writer copies payload (up to IO_REQUEST_BUF_SIZE) into the slot at issue
// time, same reasoning as the path: the caller's buffer pointer isn't
// safely dereferenceable once the worker task's own CR3 is loaded.
int alloc_io_write_request(const char* path, u8* payload, u32 payload_len);
void free_io_request(int slot_index);
void io_worker_entry(void);

#pragma GCC visibility pop
