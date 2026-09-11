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
// data_index == task_index directly - a task already IS a thread's real
// backing state (kernel/sched/task.h), no separate g_threads[] array
// needed. See kernel/syscall/syscall.c syscalls 71-73.
#define OBJ_THREAD 10
// data_index is a slot index into g_events[]/g_mutexes[]/g_timers[]
// (proc/ipc/event|mutex|timer/), same shape OBJ_CHANNEL/OBJ_PIPE already use.
#define OBJ_EVENT 11
#define OBJ_MUTEX 12
#define OBJ_TIMER 13
// data_index is a slot index into g_open_directories[]
// (proc/ipc/directory/directory.h). Faza I point 2 item 5.
#define OBJ_DIRECTORY 14
// data_index is the device's own index directly into
// kernel/drivers/device_manager/device_manager.h's g_devices[] - same
// "no separate array needed" pattern OBJ_THREAD already established for
// g_tasks[], since device_entry already holds everything a Device
// object needs to expose.
#define OBJ_DEVICE 15
// data_index is a slot index into g_io_port_ranges[] (kernel/drivers/
// io_port_range/io_port_range.h) - a real capability-gated port I/O
// grant (Faza I point 14, item 14: the CMOS RTC driver's own ring3
// isolation proof of concept). RIGHT_READ = inb allowed, RIGHT_WRITE =
// outb allowed, both checked against the port itself falling inside the
// granted [port_start, port_end] range, not just handle possession.
#define OBJ_IO_PORT_RANGE 16

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
