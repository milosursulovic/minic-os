#pragma once

#include "../../types.h"
#include "../process.h"

#pragma GCC visibility push(hidden)

#define OBJ_PROCESS 1
#define OBJ_CHANNEL 2
#define OBJ_IO_REQUEST 3
#define OBJ_NET_PING_REQUEST 4
#define OBJ_NET_TCP_REQUEST 5

typedef struct {
    bool used;
    int type;
    int data_index;  // index into g_processes[] or g_channels[], per type
} kernel_object;

extern kernel_object g_objects[8];
extern int g_object_count;

// Per-handle rights bitmask, checked before the object is touched.
#define RIGHT_QUERY 1
#define RIGHT_SEND 2
#define RIGHT_RECEIVE 4

typedef struct {
    bool used;
    int object_index;
    int rights;
} handle;

#define HANDLES_PER_PROCESS 8

extern handle g_handle_tables[MAX_PROCESSES][8];

int alloc_object(int type, int data_index);
void free_object(int object_index);
// First allocation into a fresh table always lands in slot 0 - used for
// "handle 0 = myself". rights is fixed forever at grant time.
int alloc_handle(int process_index, int object_index, int rights);
void free_handle(int process_index, int handle_index);

#pragma GCC visibility pop
