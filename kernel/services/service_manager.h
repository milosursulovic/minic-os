#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

// A real, named, generic Service Manager - "service start/stop/restart/
// status <name>", the roadmap's own literal text. proc/demo/init/init.c
// already proves the underlying crash-restart mechanism end-to-end
// (spawn, poll for real exit, respawn) but hardcoded to one service and
// ring3-side (it goes through syscalls only because ring3 code has no
// other way to see process state) - this lives in the kernel instead,
// where checking a real exit is just `!g_processes[i].used`, the exact
// flag process_exit() itself clears.
//
// "stop" is scoped honestly: this kernel has no way to forcibly
// terminate another process from outside (only self-exit via syscall 12
// exists) - real preemptive cross-process kill is a separate, bigger
// feature. stop means "don't respawn the next time this exits", not a
// fake instant kill that doesn't actually exist yet.
#define SERVICE_SLOTS 8

typedef struct {
    bool used;
    char name[32];
    u8* image_start;
    u8* image_end;
    bool auto_restart;
    bool running;
    int process_index;   // valid only while running
    u32 restart_count;   // real, observable - bumped only on a genuine respawn
} service_entry;

extern service_entry g_services[SERVICE_SLOTS];

// Adds a named definition, not yet running - matches real service
// managers (registered != started).
int service_register(const char* name, u8* image_start, u8* image_end, bool auto_restart);
bool service_start(const char* name);
// Real, honest "don't bring it back" - see the file comment above.
bool service_stop(const char* name);
bool service_restart(const char* name);
bool service_get_status(const char* name, bool* running_out, u32* restart_count_out, int* process_index_out);

// By-index enumeration (mirrors kernel/drivers/device_manager/device_manager.h's
// device_manager_get shape) - service_get_status/find_service are name-based
// only, nothing before this could list "every registered service" for a
// GUI to render as rows. flags_out packs used(bit0)/running(bit1)/
// auto_restart(bit2), same multi-value packing style syscall 42 sys_time
// already uses. Returns false for an out-of-range or unused slot.
bool service_list_entry(int index, char* name_out, u32* flags_out, u32* restart_count_out);

// Background kernel task (kmain.c, same shape as io_worker_entry/etc) -
// loops forever, respawning any used&&running&&auto_restart service
// whose process has genuinely exited.
void service_manager_worker_entry(void);

#pragma GCC visibility pop
