#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

int device_read(const char* name, u8* buf, u32 max_len);
// devfs is flat (no real subdirectories) - lists whatever pseudo-files
// actually exist (just "ticks" so far). size_out is always 0 (these
// aren't real on-disk files with a byte length until read).
bool devfs_list_entry(int index, char* name_out, u32* size_out, bool* is_dir_out);

#pragma GCC visibility pop
