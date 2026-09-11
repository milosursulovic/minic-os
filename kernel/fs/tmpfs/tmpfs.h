#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// VFS backend for /temp (Faza I point 6, item 12) - a real, genuinely
// ephemeral RAM-backed filesystem: a fixed in-kernel table, never
// touching disk, so it's cleared on every boot for free (kernel BSS
// resets), true tmpfs semantics rather than just another MiniFS folder.
// Flat, no real subdirectories - same convention devfs.h already
// documents for a non-disk backend. No owner/mode/permission concept
// (no permission gate), matching devfs/procfs's own established
// "no permission checks" convention for a non-MiniFS backend.
#define TMPFS_SLOTS 8
#define TMPFS_MAX_SIZE 4096
// Names cap at the same 19+nul as MiniFS's dir_entry.name - callers
// (e.g. shell/shell/commands/fs.c's cmd_ls) size their name_out buffers
// against that existing convention.

// Returns bytes read, or -1 if name doesn't exist, -2 if max_len is too
// small for the real stored size.
int tmpfs_read(const char* name, u8* buf, u32 max_len);
// Create-or-overwrite (unlike MiniFS's create-only-fails-if-exists) - a
// real, honest, documented difference: temp-file usage wants a plain
// overwrite, not a delete-then-recreate dance. Fails only if every slot
// is already used by a DIFFERENT name.
bool tmpfs_write(const char* name, u8* data, u32 len);
bool tmpfs_delete(const char* name);
// index is a raw slot number (0..TMPFS_SLOTS-1), same "false past an
// unused slot" contract minifs.c's fs_list_entry already uses.
bool tmpfs_list_entry(int index, char* name_out, u32* size_out, bool* is_dir_out);
bool tmpfs_stat(const char* name, u32* size_out, bool* is_dir_out);

#pragma GCC visibility pop
