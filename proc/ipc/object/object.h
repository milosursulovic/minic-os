#pragma once

#include "../../../types.h"
#include "../../process.h"

#pragma GCC visibility push(hidden)

#define OBJ_PROCESS 1
#define OBJ_CHANNEL 2
#define OBJ_IO_REQUEST 3
#define OBJ_NET_PING_REQUEST 4
#define OBJ_NET_TCP_REQUEST 5
#define OBJ_FILE 6
#define OBJ_PIPE 7
#define OBJ_SHARED_MEMORY 8
#define OBJ_SOCKET 9

typedef struct {
    bool used;
    int type;
    int data_index;  // index into g_processes[]/g_channels[]/g_open_files[], per type
} kernel_object;

// Every process gets at least one self-object (proc/process.c's "handle 0
// = myself") - with MAX_PROCESSES=8 that alone can nearly fill a small
// table, leaving no room for channels/io_requests/file objects together.
// Same "bump the cap before the new consumer starves everyone else"
// lesson as MAX_TASKS/MAX_PROCESSES earlier this session.
#define OBJECT_SLOTS 32
extern kernel_object g_objects[OBJECT_SLOTS];
extern int g_object_count;

// Per-handle rights bitmask, checked before the object is touched.
#define RIGHT_QUERY 1
#define RIGHT_SEND 2
#define RIGHT_RECEIVE 4
#define RIGHT_READ 8
#define RIGHT_WRITE 16
// The roadmap's own point 5 text ("Handle<File> READ/WRITE/MAP") - real
// now, for SharedMemory (proc/ipc/shared_memory/shared_memory.h).
#define RIGHT_MAP 32

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
