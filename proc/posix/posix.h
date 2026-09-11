#pragma once

// A real POSIX compatibility shim (Faza I point 13) - open()/read()/
// write()/close()/lseek() from the roadmap's own point 13 text, plus a
// real errno, O_RDWR, SEEK_CUR/SEEK_END, unlink(), stat(), and fcntl()
// (Faza I point 13, item 11 of the round-2 completion order). Still a
// thin second skin over the already-tested File-object syscalls (44-48,
// see gui_toolkit.h's gt_file_* wrappers) and the new VFS stat/unlink
// syscalls (97/98) underneath - not a duplicate implementation, not a
// real libc. What's still deliberately NOT here: no O_CREAT/O_TRUNC/
// O_APPEND flags (open()'s own permissive "just works" semantics already
// cover create-if-missing and truncate-on-write-open), no F_SETFL mutable
// flags (this kernel has no non-blocking I/O or O_APPEND to toggle), no
// dup()/fork() interaction for file descriptors (this shim's own fd
// table is per-open()-call only, never inherited or duplicated).

#include "../../types.h"
#include "../gui_toolkit.h"

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Real POSIX/Linux numeric values, not made up - so a program written
// against this shim that happens to hardcode these numbers (instead of
// the symbolic names) still works.
#define EIO 5
#define EBADF 9
#define ENOENT 2
#define EACCES 13
#define EINVAL 22

// static (not a real per-thread errno_location()) - keeps this a safe
// tentative definition even if some future program includes this header
// from more than one of its own translation units (GCC's -fno-common
// default would otherwise turn a second plain global definition into a
// real link error). Real thread-local errno would need a per-thread
// storage mechanism this kernel doesn't have yet - a known, documented
// simplification, not a silent gap.
static int errno;

// fcntl(F_GETFL) needs to answer with the access mode a fd was opened
// with. Rather than add a new syscall just to ask the kernel something
// this shim's own open() already knows, track it here - safe precisely
// because this shim's fd never outlives or gets duplicated past what
// open()/close() here already manage (no dup()/fork()-across-fd
// semantics exist in this shim to break that invariant).
#define POSIX_MAX_FDS 8  // mirrors proc/ipc/object/object.h's HANDLES_PER_PROCESS
static int g_posix_fd_access[POSIX_MAX_FDS];

static __attribute__((unused)) int open(const char* path, int flags) {
    int fd = gt_file_open(path, flags);
    if (fd < 0) {
        errno = fd == -2 ? EACCES : ENOENT;
        return -1;
    }
    if (fd < POSIX_MAX_FDS) {
        g_posix_fd_access[fd] = flags;
    }
    return fd;
}

static __attribute__((unused)) int read(int fd, void* buf, u32 count) {
    int n = gt_file_read(fd, (u8*) buf, count);
    if (n < 0) {
        errno = EBADF;
        return -1;
    }
    return n;
}

static __attribute__((unused)) int write(int fd, const void* buf, u32 count) {
    int n = gt_file_write(fd, (const u8*) buf, count);
    if (n < 0) {
        errno = EBADF;
        return -1;
    }
    return n;
}

// Real POSIX lseek() - SEEK_SET/CUR/END, signed offset, returns the
// resulting absolute offset (not just 0/-1) straight from
// file_object_seek()'s own real cursor arithmetic (kernel/ipc/file/
// file.c), which now does the SEEK_CUR/END math itself so this stays a
// single round trip, not a racy separate query-then-seek.
static __attribute__((unused)) int lseek(int fd, i64 offset, int whence) {
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        errno = EINVAL;
        return -1;
    }
    i64 result = gt_file_seek(fd, offset, whence);
    if (result < 0) {
        errno = EINVAL;
        return -1;
    }
    return (int) result;
}

static __attribute__((unused)) int close(int fd) {
    bool ok = gt_file_close(fd);
    if (!ok) {
        errno = EIO;
        return -1;
    }
    return 0;
}

// Real unlink() (Faza I point 13, item 11) - wraps the new VFS-aware
// delete (syscall 98, gt_vfs_unlink), a real permission-gated removal
// (MODE_OWNER_ONLY_WRITE, same as write()'s own underlying check) unlike
// gt_fs_delete's raw, VFS-unaware, unprotected removal.
static __attribute__((unused)) int unlink(const char* path) {
    if (!gt_vfs_unlink((char*) path)) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

// Explicitly NOT the real POSIX st_mode bit layout (S_IFREG/S_IFDIR/
// permission bits) - this kernel's own file "mode" already means
// something different (MODE_OWNER_ONLY_READ/WRITE, a restriction mask,
// not a Unix permission field). st_mode here carries those same bits
// verbatim; st_is_dir is a separate, honest field instead of overloading
// st_mode with fabricated S_IFDIR-style values that would mean nothing
// real on this filesystem.
typedef struct {
    u32 st_size;
    bool st_is_dir;
    u8 st_owner_uid;
    u8 st_mode;
} stat_t;

// Real stat() (Faza I point 13, item 11) - wraps the new VFS-aware stat
// (syscall 97, gt_vfs_stat). No permission check needed, matching real
// POSIX stat() (data-access permission isn't required just to see a
// file's metadata).
static __attribute__((unused)) int stat(const char* path, stat_t* out) {
    if (!gt_vfs_stat((char*) path, &out->st_size, &out->st_is_dir, &out->st_owner_uid, &out->st_mode)) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

#define F_GETFL 1
#define F_SETFL 2

// Real fcntl() (Faza I point 13, item 11) - F_GETFL answers with the
// real access mode this shim's own open() recorded for fd. F_SETFL is an
// honest "not supported" (-1), not a fake success - this kernel has no
// mutable file-status flag (O_NONBLOCK/O_APPEND) to actually change yet.
static __attribute__((unused)) int fcntl(int fd, int cmd) {
    if (cmd == F_GETFL) {
        if (fd < 0 || fd >= POSIX_MAX_FDS) {
            errno = EBADF;
            return -1;
        }
        return g_posix_fd_access[fd];
    }
    if (cmd == F_SETFL) {
        errno = EINVAL;
        return -1;
    }
    errno = EINVAL;
    return -1;
}
