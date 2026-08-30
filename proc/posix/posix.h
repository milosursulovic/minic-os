#pragma once

// A real, deliberately minimal POSIX compatibility shim - the exact
// open()/read()/write()/close() example from the roadmap's own point 13
// text, plus lseek() since the File-object syscalls underneath
// (44-48, see gui_toolkit.h's gt_file_* wrappers) already have a real
// seekable cursor. This is a thin second skin over those same, already-
// tested syscalls - not a duplicate implementation, not a real libc: no
// errno, no O_RDWR (a File object is opened read-only or write-only,
// never both - gui_toolkit.h's own gt_file_write() doc comment already
// states this), no SEEK_CUR/SEEK_END (only SEEK_SET maps cleanly onto
// file_object_seek()'s absolute-position semantics without exposing a
// new "get current length" primitive), no unlink()/stat()/fcntl() or
// anything else libc-shaped - a bigger surface than what was asked for
// this first pass.

#include "../../types.h"
#include "../gui_toolkit.h"

#define O_RDONLY 0
#define O_WRONLY 1

// SEEK_CUR/SEEK_END are deliberately not defined - passing one to
// lseek() is a compile error here, not a silently wrong position.
#define SEEK_SET 0

static __attribute__((unused)) int open(const char* path, int flags) {
    return gt_file_open(path, flags == O_WRONLY ? 1 : 0);
}

static __attribute__((unused)) int read(int fd, void* buf, u32 count) {
    return gt_file_read(fd, (u8*) buf, count);
}

static __attribute__((unused)) int write(int fd, const void* buf, u32 count) {
    return gt_file_write(fd, (const u8*) buf, count);
}

// Real POSIX lseek() returns the resulting offset on success, not just
// 0/-1 - since only SEEK_SET is supported, that's always just `offset`
// itself once gt_file_seek() confirms it was valid.
static __attribute__((unused)) int lseek(int fd, u32 offset, int whence) {
    if (whence != SEEK_SET) {
        return -1;
    }
    return gt_file_seek(fd, offset) ? (int) offset : -1;
}

static __attribute__((unused)) int close(int fd) {
    return gt_file_close(fd) ? 0 : -1;
}
