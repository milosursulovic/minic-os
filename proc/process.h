#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

typedef struct {
    bool used;
    u64 cr3;
    int task_index;
} process;

extern process g_processes[4];
extern int g_process_count;

void process_entry_trampoline(void);
int spawn_process(u8* image_start, u8* image_end, u64 load_vaddr, u64 stack_vaddr);
int spawn_process_from_path(const char* path, u64 load_vaddr, u64 stack_vaddr);

#pragma GCC visibility pop
