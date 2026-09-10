#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Raw path-based VFS/MiniFS operations - 4/5 (vfs_read/write), 37
// (fs_list), 38/39 (fs_delete/mkdir), 50/51 (fs_set_owner/mode).
bool syscall_vfs(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
