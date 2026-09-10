#pragma once
#include "core.h"

// Raw path-based VFS/MiniFS operations - syscalls 4, 5, 37, 38, 39, 40.
// One-shot open+op+close, unlike proc/gui_toolkit/file.h's real
// persistent file objects. Split out of the former single gui_toolkit.h.

typedef struct __attribute__((packed)) {
    char* dir_path;
    int index;
    char* name_out;
    u32* size_out;
    bool* is_dir_out;
} gt_fs_list_args;

typedef struct __attribute__((packed)) {
    u32* total_frames_out;
    u32* free_frames_out;
    u32* disk_file_count_out;
} gt_sys_info_args;

// Lists one entry (0..MINIFS_MAX_FILES-1) of dir_path ("" = /system
// root). Returns false past the last used slot or an unresolvable
// dir_path - same as fs_list_entry (kernel/fs/minifs.c), which this wraps
// via syscall 37.
static __attribute__((unused)) bool gt_fs_list(char* dir_path, int index, char* name_out, u32* size_out, bool* is_dir_out) {
    gt_fs_list_args args;
    args.dir_path = dir_path;
    args.index = index;
    args.name_out = name_out;
    args.size_out = size_out;
    args.is_dir_out = is_dir_out;
    return gt_syscall(37, (u64) &args, 0, 0) != 0;
}

// Flat removal (no child-cascade check on a directory) - wraps
// fs_delete_file via syscall 38.
static __attribute__((unused)) bool gt_fs_delete(char* path) {
    return gt_syscall(38, (u64) path, 0, 0) != 0;
}

// Wraps fs_create_dir via syscall 39.
static __attribute__((unused)) bool gt_fs_mkdir(char* path) {
    return gt_syscall(39, (u64) path, 0, 0) != 0;
}

// Wraps syscall 5 (vfs_write) - create-only-fails-if-exists, same as
// every other vfs_write caller (cmd_mkfile, install, ...).
static __attribute__((unused)) bool gt_vfs_write(char* path, u8* data, u32 len) {
    return gt_syscall(5, (u64) path, (u64) data, (u64) len) != (u64) -1;
}

// Wraps syscall 4 (vfs_read) - same raw (path, buf, max_len) shape as
// ring3prog.c's own direct do_syscall(4, ...) call. Returns bytes read,
// -1 (not found) or -2 (too big for max_len).
static __attribute__((unused)) int gt_vfs_read(char* path, u8* buf, u32 max_len) {
    return (int) gt_syscall(4, (u64) path, (u64) buf, (u64) max_len);
}

// Wraps syscall 40 - live kernel stats (frame allocator + MiniFS
// superblock), read straight from kernel globals, no caching.
static __attribute__((unused)) bool gt_sys_info(u32* total_frames_out, u32* free_frames_out, u32* disk_file_count_out) {
    gt_sys_info_args args;
    args.total_frames_out = total_frames_out;
    args.free_frames_out = free_frames_out;
    args.disk_file_count_out = disk_file_count_out;
    return gt_syscall(40, (u64) &args, 0, 0) == 0;
}
