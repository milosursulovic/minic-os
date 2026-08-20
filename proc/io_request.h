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
    char path[64];
    u8 buffer[IO_REQUEST_BUF_SIZE];
    int result;  // byte count, or -1 on failure - only meaningful once done
} io_request;

extern io_request g_io_requests[IO_REQUEST_SLOTS];

int alloc_io_request(const char* path);
void free_io_request(int slot_index);
void io_worker_entry(void);

#pragma GCC visibility pop
