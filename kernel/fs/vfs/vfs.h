#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

extern const u32 BACKEND_MINIFS;
extern const u32 BACKEND_DEVICE;
extern const u32 BACKEND_PROCFS;
extern const u32 BACKEND_TMPFS;
// A pure listing view (Faza I point 6, item 12) - /volumes shows the
// registered mount table itself (there's only one real disk in this
// kernel, so a real, honest /volumes is "here's what's mounted", not a
// fabricated volume concept). Read/write/stat/delete all refuse (false/
// -1) - it's a listing, not a place files live.
extern const u32 BACKEND_MOUNTS;

bool vfs_mount(const char* prefix, u32 backend);
// Same as vfs_mount, but backend_root is a subdirectory *within* the
// backend's own namespace to prepend after stripping the VFS prefix -
// e.g. prefix "/apps", backend_root "apps" makes "/apps/foo.bin" resolve
// to MiniFS path "apps/foo.bin" instead of colliding with /system's own
// root. "" (what vfs_mount passes) means "the backend's own root",
// unchanged from before this existed.
bool vfs_mount_at(const char* prefix, u32 backend, const char* backend_root);
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
// MiniFS-backed mounts only (same scope as vfs_stat/vfs_delete).
bool vfs_mkdir(const char* path);
// True if path resolves to a mount whose backend actually accepts
// writes (MiniFS or tmpfs) - the generic replacement for the several
// shell commands (shell/shell/commands/fs.c's touch/mkdir/cp/mv) that
// used to hardcode "starts_with(path, \"/system\")" before /apps and
// /users (Faza I point 6, item 12) existed as real, separate MiniFS-
// backed mounts too.
bool vfs_is_writable(const char* path);
// True + fills out with the real bare MiniFS-relative path (backend_root
// combined in) if path resolves to a MiniFS-backed mount - for the one
// remaining raw-MiniFS caller that has no VFS wrapper of its own
// (shell/editor/editor.c, "completely VFS-unaware" by its own design;
// tmpfs isn't editable through it - an explicit, documented scope
// limit). out should be at least 128 bytes.
bool vfs_resolve_minifs_path(const char* path, char* out);

#pragma GCC visibility pop
