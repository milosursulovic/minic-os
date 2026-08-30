#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

// 6 processes are already spawned at boot (test_prog, init, desktop_shell,
// terminal, file_manager, settings - see kmain.c), so this needs at least
// 6 plus real headroom (not another exact-fit) for the shell's own
// "spawn"/"install" demo commands.
#define MAX_PROCESSES 8

typedef struct {
    bool used;
    u64 cr3;
    int task_index;
    // Defaults to 0 (root) for every process - see kernel/syscall/syscall.c
    // syscall 49 (sys_setuid), a real but deliberately unhardened test/demo
    // primitive for now (any process can change its own uid arbitrarily).
    // Real per-file enforcement lives in proc/ipc/file/file.c.
    u8 uid;
} process;

extern process g_processes[MAX_PROCESSES];
extern int g_process_count;

void process_entry_trampoline(void);
int spawn_process(u8* image_start, u8* image_end, u64 load_vaddr, u64 stack_vaddr);
int spawn_process_from_path(const char* path, u64 load_vaddr, u64 stack_vaddr);

#pragma GCC visibility pop
