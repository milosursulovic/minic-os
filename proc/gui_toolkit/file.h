#pragma once
#include "core.h"

// Real persistent open-file objects (proc/ipc/file/file.h via syscalls
// 44-48) - open once, read/write/seek incrementally, close - unlike
// gt_vfs_read/gt_vfs_write's one-shot open+op+close (proc/gui_toolkit/
// vfs.h). mode 0=read/1=write/2=read+write (FILE_ACCESS_* in file.h,
// mirrored numerically by proc/posix/posix.h's O_RDONLY/O_WRONLY/O_RDWR);
// the rights the handle gets are fixed at open time (RIGHT_READ,
// RIGHT_WRITE, or both for mode 2), so e.g. a read-only handle's
// gt_file_write() call is rejected by the kernel, not just by convention.
// Split out of the former single gui_toolkit.h.

static __attribute__((unused)) int gt_file_open(const char* path, int mode) {
    u64 result = gt_syscall(44, (u64) path, (u64) mode, 0);
    if (result == (u64) -1) {
        return -1;
    }
    // A real distinct failure code (-2, permission-denied) round-trips
    // correctly through the plain int cast below too - only the exact
    // (u64) -1 case above needs special-casing.
    return (int) result;
}

static __attribute__((unused)) int gt_file_read(int handle, u8* buf, u32 max_len) {
    return (int) gt_syscall(45, (u64) handle, (u64) buf, (u64) max_len);
}

static __attribute__((unused)) int gt_file_write(int handle, const u8* data, u32 len) {
    return (int) gt_syscall(46, (u64) handle, (u64) data, (u64) len);
}

// Real POSIX SEEK_SET(0)/SEEK_CUR(1)/SEEK_END(2) semantics (Faza I point
// 13, item 11) - offset is signed. Returns the new cursor, or -1.
static __attribute__((unused)) i64 gt_file_seek(int handle, i64 offset, int whence) {
    return (i64) gt_syscall(47, (u64) handle, (u64) offset, (u64) whence);
}

static __attribute__((unused)) bool gt_file_close(int handle) {
    return gt_syscall(48, (u64) handle, 0, 0) != (u64) -1;
}

// Real UID-based file ownership/permission bits - values must match
// kernel/fs/minifs/minifs.h's MODE_OWNER_ONLY_READ/WRITE exactly (same
// kernel-constant-mirrored-in-ring3 duplication this toolkit's own
// RIGHT_QUERY-style constants already use elsewhere - ring3 code can't
// include a kernel-internal header). Enforcement itself happens in
// proc/ipc/file/file.c's file_object_open().
#define MODE_OWNER_ONLY_READ 1
#define MODE_OWNER_ONLY_WRITE 2
static __attribute__((unused)) bool gt_setuid(u8 uid) {
    return gt_syscall(49, (u64) uid, 0, 0) != (u64) -1;
}

// path is bare MiniFS-relative (e.g. "permtest.mfs"), not VFS-absolute -
// same convention gt_fs_mkdir/gt_fs_delete (proc/gui_toolkit/vfs.h)
// already use.
static __attribute__((unused)) bool gt_fs_set_owner(const char* path, u8 uid) {
    return gt_syscall(50, (u64) path, (u64) uid, 0) != (u64) -1;
}

static __attribute__((unused)) bool gt_fs_set_mode(const char* path, u8 mode) {
    return gt_syscall(51, (u64) path, (u64) mode, 0) != (u64) -1;
}
