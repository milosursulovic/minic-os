#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real, persistent open-file objects with a cursor - backs syscalls
// 44-48 (OBJ_FILE, proc/ipc/object/object.h). Unlike vfs_read/vfs_write's
// one-shot open+op+close, this lets a caller open once and read/write
// incrementally across several calls, same as a real file descriptor.
//
// MiniFS has no true random-access write (fs_write_file refuses an
// existing name - "delete then write" is the only real overwrite, the
// same pattern cp/mv/mkfile/editor already use) - so a write-mode file
// object is a real, honest buffered-write-commit-on-close design:
// accumulate bytes across write() calls into this in-kernel buffer,
// commit once at close(). A read-mode file object reads the whole file
// into the buffer at open() and serves read()/seek() from it - a real
// cursor, the actual capability that doesn't exist anywhere else yet.
#define FILE_OBJECT_SLOTS 8
#define FILE_MAX_SIZE 4096

typedef struct {
    bool used;
    bool write_mode;
    char path[128];
    u8 buffer[FILE_MAX_SIZE];
    u32 length;  // read-mode: real file size at open time. write-mode: bytes accumulated so far.
    u32 cursor;  // read-mode only.
    // Recorded from open()'s caller_uid - real Unix "creator becomes
    // owner" semantics, applied for real at close() time (see
    // file_object_close()).
    u8 owner_uid;
} open_file;

extern open_file g_open_files[FILE_OBJECT_SLOTS];

// Read-mode: reads the whole file via vfs_read at open time. Write-mode:
// starts empty (nothing touches disk until close()). caller_uid is the
// opening process's uid (proc/process.h) - for a path under /system with
// a real owner_uid/mode already set (kernel/fs/minifs/minifs.h), refuses
// (-1) if the relevant MODE_OWNER_ONLY_* bit is set and caller_uid is
// neither the owner nor root (uid 0, which always bypasses - real UNIX
// semantics). A not-yet-existing path (about to be created by a
// write-mode open) has no real owner yet, so this check is skipped.
// Returns a slot index, or -1.
int file_object_open(const char* path, bool write_mode, u8 caller_uid);
// Copies min(max_len, length-cursor) bytes from the cursor, advances it.
// Returns the byte count (0 at real EOF).
int file_object_read(int slot, u8* out, u32 max_len);
// Appends into the buffer (bounded by FILE_MAX_SIZE). Returns bytes
// actually accepted (may be less than len if the buffer is full).
int file_object_write(int slot, const u8* data, u32 len);
// Read-mode only (write-mode is append-only accumulation) - repositions
// the cursor, bounds-checked against length.
bool file_object_seek(int slot, u32 pos);
// Write-mode: commits the accumulated buffer to real MiniFS storage
// (delete-then-write, the established overwrite pattern). Read-mode:
// no-op commit. Always frees the slot either way.
bool file_object_close(int slot);

#pragma GCC visibility pop
