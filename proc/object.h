#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

// A real const - no OBJ_NONE constant - an unallocated kernel_object's
// zero-valued `type` field is never read (guarded by `used` everywhere),
// so there's nothing for a "none" tag to ever be compared against.
#define OBJ_PROCESS 1
// The first object type actually wrapped by real, enforced per-handle
// rights (see RIGHT_* below) rather than "any handle to this object can
// do anything the object supports."
#define OBJ_CHANNEL 2

typedef struct {
    bool used;
    int type;
    int data_index;  // meaning depends on type - for OBJ_PROCESS, an index into
                      // g_processes[]; for OBJ_CHANNEL, an index into g_channels[]
} kernel_object;

extern kernel_object g_objects[8];
extern int g_object_count;

// Real per-handle rights, the actual "capability" in
// "capability/permission system" - a bitmask of what operations THIS
// handle specifically permits, checked at the syscall boundary before
// the underlying object is touched at all. A single bit space is shared
// across object types on purpose (simpler than a per-type rights enum) -
// safe in practice since a handle's type is always checked first anyway.
#define RIGHT_QUERY 1     // e.g. syscall 3's handle-query, on an OBJ_PROCESS handle
#define RIGHT_SEND 2      // channel.send(), on an OBJ_CHANNEL handle
#define RIGHT_RECEIVE 4   // channel.receive(), on an OBJ_CHANNEL handle

typedef struct {
    bool used;
    int object_index;
    int rights;
} handle;

#define HANDLES_PER_PROCESS 8

// 4 processes * 8 handles each.
extern handle g_handle_tables[4][8];

int alloc_object(int type, int data_index);
// Every process's handle table starts empty, so the FIRST handle ever
// allocated into it always lands in slot 0 - spawn_process() relies on
// this to give every process a well-known "handle 0 = myself" without
// needing a dedicated syscall just to look it up. `rights` is set once,
// here, at grant time - never widened later, so whatever policy the
// granting code chooses is exactly what the handle can ever do for its
// whole lifetime.
int alloc_handle(int process_index, int object_index, int rights);

#pragma GCC visibility pop
