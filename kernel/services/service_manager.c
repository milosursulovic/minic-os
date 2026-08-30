#include "service_manager.h"
#include "../../proc/process.h"
#include "../sched/task.h"
#include "../lib/strings.h"
#include "../lib/rand.h"

service_entry g_services[SERVICE_SLOTS];

// Real ASLR (kernel/lib/rand.h) - same base every other builtin-style
// program uses (kernel/syscall/syscall.c's BUILTIN_LOAD_BASE, kmain.c's
// own spawn calls, etc), but load_vaddr is randomized per spawn/respawn,
// not the same fixed address every time.
static const u64 SERVICE_LOAD_BASE = 0x80000000;

static void copy_bounded(char* dst, const char* src, int cap) {
    int i = 0;
    while (i < cap - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i = i + 1;
    }
    dst[i] = '\0';
}

static int find_service(const char* name) {
    int i = 0;
    while (i < SERVICE_SLOTS) {
        if (g_services[i].used && streq(g_services[i].name, name)) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

int service_register(const char* name, u8* image_start, u8* image_end, bool auto_restart) {
    int existing = find_service(name);
    if (existing >= 0) {
        return existing;
    }
    int i = 0;
    while (i < SERVICE_SLOTS) {
        if (!g_services[i].used) {
            copy_bounded(g_services[i].name, name, 32);
            g_services[i].image_start = image_start;
            g_services[i].image_end = image_end;
            g_services[i].auto_restart = auto_restart;
            g_services[i].running = false;
            g_services[i].process_index = -1;
            g_services[i].restart_count = 0;
            g_services[i].used = true;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

bool service_start(const char* name) {
    int slot = find_service(name);
    if (slot < 0) {
        return false;
    }
    if (g_services[slot].running) {
        return true;  // already running - starting again is a no-op success
    }
    u64 load_vaddr = randomize_load_vaddr(SERVICE_LOAD_BASE);
    int proc_index = spawn_process(g_services[slot].image_start, g_services[slot].image_end,
                                    load_vaddr, load_vaddr + 0x20000);
    if (proc_index < 0) {
        return false;
    }
    g_services[slot].process_index = proc_index;
    g_services[slot].running = true;
    return true;
}

bool service_stop(const char* name) {
    int slot = find_service(name);
    if (slot < 0) {
        return false;
    }
    g_services[slot].auto_restart = false;
    return true;
}

bool service_restart(const char* name) {
    int slot = find_service(name);
    if (slot < 0) {
        return false;
    }
    g_services[slot].auto_restart = true;
    return service_start(name);
}

bool service_get_status(const char* name, bool* running_out, u32* restart_count_out, int* process_index_out) {
    int slot = find_service(name);
    if (slot < 0) {
        return false;
    }
    // A pure read - must NOT mutate g_services[slot].running here. The
    // worker (service_manager_worker_entry) is the sole writer of that
    // field's false->respawn transition; a status check that also flips it
    // false races the worker and can permanently starve it (whichever
    // sees the exit first "wins" - if status wins, the worker's own
    // used&&running&&auto_restart condition never becomes true again).
    bool actually_alive = g_services[slot].running && g_processes[g_services[slot].process_index].used;
    *running_out = actually_alive;
    *restart_count_out = g_services[slot].restart_count;
    *process_index_out = actually_alive ? g_services[slot].process_index : -1;
    return true;
}

bool service_list_entry(int index, char* name_out, u32* flags_out, u32* restart_count_out) {
    if (index < 0 || index >= SERVICE_SLOTS || !g_services[index].used) {
        return false;
    }
    copy_bounded(name_out, g_services[index].name, 32);
    bool actually_alive = g_services[index].running && g_processes[g_services[index].process_index].used;
    u32 flags = 1;  // used
    if (actually_alive) {
        flags = flags | 2;
    }
    if (g_services[index].auto_restart) {
        flags = flags | 4;
    }
    *flags_out = flags;
    *restart_count_out = g_services[index].restart_count;
    return true;
}

void service_manager_worker_entry(void) {
    for (;;) {
        int i = 0;
        while (i < SERVICE_SLOTS) {
            if (g_services[i].used && g_services[i].running && g_services[i].auto_restart) {
                if (!g_processes[g_services[i].process_index].used) {
                    // Real exit detected - respawn, same mechanism
                    // proc/demo/init/init.c already proved works, just
                    // generalized to any registered service and checked
                    // directly (kernel-side) instead of via syscall query.
                    u64 load_vaddr = randomize_load_vaddr(SERVICE_LOAD_BASE);
                    int proc_index = spawn_process(g_services[i].image_start, g_services[i].image_end,
                                                    load_vaddr, load_vaddr + 0x20000);
                    if (proc_index >= 0) {
                        g_services[i].process_index = proc_index;
                        g_services[i].restart_count = g_services[i].restart_count + 1;
                    } else {
                        g_services[i].running = false;  // out of process slots - stop trying this tick
                    }
                }
            }
            i = i + 1;
        }
        yield();
    }
}
