#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

extern const u32 BACKEND_MINIFS;
extern const u32 BACKEND_DEVICE;
extern const u32 BACKEND_PROCFS;

bool vfs_mount(const char* prefix, u32 backend);
// Faza I point 5 item 7: caller_uid is checked against a MiniFS-backed
// path's real owner_uid/mode (kernel/fs/minifs/minifs.h) before the
// read/write is allowed - same MODE_OWNER_ONLY_READ/WRITE enforcement
// proc/ipc/file/file.c's file_object_open() already has, now real for
// this older raw path too. A path with no owner/mode set yet (or on a
// non-MiniFS mount) is unaffected - no restriction to check.
int vfs_read(const char* path, u8* buf, u32 max_len, u8 caller_uid);
bool vfs_write(const char* path, u8* data, u32 len, u8 caller_uid);
// dir_path == "" is the real VFS root itself - lists the registered mount
// points (their own prefixes, minus the leading '/') as directories, so
// a GUI file browser can navigate the whole namespace from "/" down,
// not just one already-known mount. Otherwise resolves dir_path to its
// owning mount and delegates to that backend's own listing.
bool vfs_list_entry(const char* dir_path, int index, char* name_out, u32* size_out, bool* is_dir_out);

// Real POSIX stat()/unlink() backing (Faza I point 13, item 11) -
// MiniFS-backed mounts only, same scope limit vfs_write already has
// (device/procfs paths have no real on-disk entry to report on or
// remove, so both return false for them - explicit, not silent).
// vfs_stat needs no permission check (real POSIX stat() doesn't need
// data-access permission either, just path lookup).
bool vfs_stat(const char* path, u32* size_out, bool* is_dir_out, u8* owner_uid_out, u8* mode_out);
// Gated the same way vfs_write is - refuses if MODE_OWNER_ONLY_WRITE is
// set and caller_uid is neither the owner nor root.
bool vfs_delete(const char* path, u8 caller_uid);

#pragma GCC visibility pop
