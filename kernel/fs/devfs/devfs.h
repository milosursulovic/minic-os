#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// name may now be "ticks", or "<category>/<device name>" (Faza I point
// 9, item 13 - a real nested device tree instead of one flat pseudo-file).
int device_read(const char* name, u8* buf, u32 max_len);
// subpath == "" is the /devices root: "ticks" plus three always-present
// category directories ("pci"/"platform"/"input" - real, present even
// when a category is currently empty). subpath == one of those three
// lists that category's real kernel/drivers/device_manager entries.
// size_out is always 0 (these aren't real on-disk files with a byte
// length until read).
bool devfs_list_entry(const char* subpath, int index, char* name_out, u32* size_out, bool* is_dir_out);

#pragma GCC visibility pop
