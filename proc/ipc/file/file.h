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

// access mirrors proc/posix/posix.h's O_RDONLY(0)/O_WRONLY(1)/O_RDWR(2) -
// same kernel-constant-mirrored-in-ring3 duplication this toolkit's own
// MODE_OWNER_ONLY_* constants already use (ring3 code can't include a
// kernel-internal header, and posix.h can't include this one either).
#define FILE_ACCESS_RDONLY 0
#define FILE_ACCESS_WRONLY 1
#define FILE_ACCESS_RDWR 2

typedef struct {
    bool used;
    int access;  // FILE_ACCESS_* - was a plain write_mode bool before O_RDWR existed
    char path[128];
    u8 buffer[FILE_MAX_SIZE];
    u32 length;  // real file size (RDONLY/RDWR) or bytes accumulated so far (WRONLY).
    u32 cursor;  // RDONLY/RDWR only - real seekable position.
    // Recorded from open()'s caller_uid - real Unix "creator becomes
    // owner" semantics, applied for real at close() time (see
    // file_object_close()).
    u8 owner_uid;
} open_file;

extern open_file g_open_files[FILE_OBJECT_SLOTS];

// RDONLY: reads the whole file via vfs_read at open time, fails if it
// doesn't exist. WRONLY: starts empty (nothing touches disk until
// close()), same permissive "no O_CREAT needed" convention as always.
// RDWR: loads existing content if the path exists (real random-access
// read+write over it), or starts empty if it doesn't (creates on
// close(), same as WRONLY) - never fails just because the path is new.
// caller_uid is the opening process's uid (proc/process.h) - for a path
// under /system with a real owner_uid/mode already set (kernel/fs/minifs/
// minifs.h), the relevant MODE_OWNER_ONLY_* bit (READ for RDONLY/RDWR,
// WRITE for WRONLY/RDWR - both checked for RDWR) is enforced unless
// caller_uid is the owner or root (uid 0). A not-yet-existing path has no
// real owner yet, so this check is skipped for it.
// Returns a slot index, or a real distinct negative failure code: -1 for
// not-found/other, -2 specifically for permission-denied - this is what
// lets proc/posix/posix.h's open() set a real errno instead of guessing.
int file_object_open(const char* path, int access, u8 caller_uid);
// Copies min(max_len, length-cursor) bytes from the cursor, advances it.
// Returns the byte count (0 at real EOF).
int file_object_read(int slot, u8* out, u32 max_len);
// Writes starting at the current cursor (bounded by FILE_MAX_SIZE),
// extending length if the write reaches past it - a real random-access
// write, not just append. Advances the cursor by the bytes actually
// written. Returns the byte count actually accepted.
int file_object_write(int slot, const u8* data, u32 len);
// Real POSIX SEEK_SET(0)/SEEK_CUR(1)/SEEK_END(2) semantics - base is 0,
// the current cursor, or length respectively; offset is signed. Refused
// for WRONLY (append-only accumulation has no real cursor concept) or if
// the resulting position would fall outside [0, length]. Returns the new
// cursor, or -1 on failure.
i64 file_object_seek(int slot, i64 offset, int whence);
// WRONLY/RDWR: commits the buffer to real MiniFS storage (delete-then-
// write, the established overwrite pattern). RDONLY: no-op commit.
// Always frees the slot either way.
bool file_object_close(int slot);

#pragma GCC visibility pop
