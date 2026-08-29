#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// VFS backend for /processes: not disk-backed, reflects live kernel state
// (like /devices' devfs) - one pseudo-file per process table slot
// (proc/process.h's g_processes[]), named "procN" where N is the raw
// slot index (same "index is a raw slot number, false if unused" contract
// kernel/fs/minifs.c's fs_list_entry already uses, not a separately
// counted position).
bool procfs_list_entry(int index, char* name_out, u32* size_out, bool* is_dir_out);
// name must be "procN" - the same real fields the console shell's own
// `ps` command already prints (task_index, cr3).
int procfs_read(const char* name, u8* buf, u32 max_len);

#pragma GCC visibility pop
