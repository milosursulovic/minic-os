// A generic kernel object model + per-process handle tables - the
// NT-style piece of this kernel's architecture. Deliberately not
// introduced any earlier: a handle only means something once there's a
// real user/kernel boundary worth protecting (ring3+syscalls) and real
// per-process state worth wrapping (per-process address spaces) -
// before that, everything was ring 0 and saw everything anyway, so a
// handle indirection would have been pure ceremony.
//
// Two tables, matching the real NT design this is modeled on: a single
// kernel-wide object table (g_objects) holding the actual objects, and a
// SEPARATE table per process (g_handle_tables) of small integers that
// index into it. Ring3 code only ever sees the small integer - it can
// never walk straight into g_objects[] itself; every syscall that
// resolves a handle does its own inline bounds/existence/rights check
// directly against g_handle_tables rather than through a shared helper,
// since rights checking needs the handle's `rights` field, not just its
// object_index.

#include "object.h"

kernel_object g_objects[8];
int g_object_count;

handle g_handle_tables[4][8];

int alloc_object(int type, int data_index) {
    int i = 0;
    while (i < 8) {
        if (!g_objects[i].used) {
            g_objects[i].used = true;
            g_objects[i].type = type;
            g_objects[i].data_index = data_index;
            g_object_count = g_object_count + 1;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

int alloc_handle(int process_index, int object_index, int rights) {
    int i = 0;
    while (i < HANDLES_PER_PROCESS) {
        if (!g_handle_tables[process_index][i].used) {
            g_handle_tables[process_index][i].used = true;
            g_handle_tables[process_index][i].object_index = object_index;
            g_handle_tables[process_index][i].rights = rights;
            return i;
        }
        i = i + 1;
    }
    return -1;
}
