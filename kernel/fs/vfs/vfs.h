#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

extern const u32 BACKEND_MINIFS;
extern const u32 BACKEND_DEVICE;
extern const u32 BACKEND_PROCFS;

bool vfs_mount(const char* prefix, u32 backend);
int vfs_read(const char* path, u8* buf, u32 max_len);
bool vfs_write(const char* path, u8* data, u32 len);
// dir_path == "" is the real VFS root itself - lists the registered mount
// points (their own prefixes, minus the leading '/') as directories, so
// a GUI file browser can navigate the whole namespace from "/" down,
// not just one already-known mount. Otherwise resolves dir_path to its
// owning mount and delegates to that backend's own listing.
bool vfs_list_entry(const char* dir_path, int index, char* name_out, u32* size_out, bool* is_dir_out);

#pragma GCC visibility pop
