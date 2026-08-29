#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

// 5 processes are already spawned at boot (test_prog, init, desktop_shell,
// terminal, file_manager - see kmain.c), so this needs at least 5 plus
// headroom for the shell's own "spawn"/"install" demo commands.
#define MAX_PROCESSES 6

typedef struct {
    bool used;
    u64 cr3;
    int task_index;
} process;

extern process g_processes[MAX_PROCESSES];
extern int g_process_count;

void process_entry_trampoline(void);
int spawn_process(u8* image_start, u8* image_end, u64 load_vaddr, u64 stack_vaddr);
int spawn_process_from_path(const char* path, u64 load_vaddr, u64 stack_vaddr);

#pragma GCC visibility pop
