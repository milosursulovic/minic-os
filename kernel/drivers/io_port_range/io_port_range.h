#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real capability-gated port I/O ranges (Faza I point 14, item 14) -
// backs OBJ_IO_PORT_RANGE (proc/ipc/object/object.h). A real, honest
// "who is allowed to touch this hardware" boundary: ring3 code can't
// execute in/out directly at CPL3 without a per-process IOPL/TSS I/O
// bitmap this kernel doesn't set up, so the mediating syscall
// (kernel/syscall/handlers/port_io.c) is the actual enforcement point -
// this table is what it checks a request's port against.
#define IO_PORT_RANGE_SLOTS 8

typedef struct {
    bool used;
    u16 port_start;
    u16 port_end;  // inclusive
} io_port_range;

extern io_port_range g_io_port_ranges[IO_PORT_RANGE_SLOTS];

// Returns the new range's slot index (the object's data_index), or -1
// if every slot is taken.
int io_port_range_create(u16 port_start, u16 port_end);
bool io_port_range_contains(int slot, u16 port);

#pragma GCC visibility pop
