// Path-namespace VFS: routes a path to whichever backend's mount prefix matches
// (`/system` -> MiniFS, `/devices` -> devfs), tag + if/else dispatch.

#include "vfs.h"
#include "minifs.h"
#include "devfs.h"
#include "../lib/strings.h"

const u32 BACKEND_MINIFS = 1;
const u32 BACKEND_DEVICE = 2;

typedef struct {
    char prefix[16];
    u32 backend;
    bool used;
} mount;

static mount g_mounts[4];
static int g_mount_count;

static void copy_prefix(char* dst, const char* src) {
    int i = 0;
    while (i < 15 && src[i] != '\0') {
        dst[i] = src[i];
        i = i + 1;
    }
    while (i < 16) {
        dst[i] = '\0';
        i = i + 1;
    }
}

bool vfs_mount(const char* prefix, u32 backend) {
    if (g_mount_count >= 4) {
        return false;
    }
    copy_prefix(g_mounts[g_mount_count].prefix, prefix);
    g_mounts[g_mount_count].backend = backend;
    g_mounts[g_mount_count].used = true;
    g_mount_count = g_mount_count + 1;
    return true;
}

static int vfs_find_mount(const char* path) {
    int i = 0;
    while (i < g_mount_count) {
        if (g_mounts[i].used && starts_with(path, g_mounts[i].prefix)) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

// "/system/hello.txt" -> "hello.txt", the bare name the backend API expects.
static const char* vfs_strip_prefix(const char* path, int mount_index) {
    int prefix_len = strlen_(g_mounts[mount_index].prefix);
    const char* rest = &path[prefix_len];
    if (rest[0] == '/') {
        rest = &rest[1];
    }
    return rest;
}

int vfs_read(const char* path, u8* buf, u32 max_len) {
    int m = vfs_find_mount(path);
    if (m < 0) {
        return -1;
    }
    const char* rest = vfs_strip_prefix(path, m);
    if (g_mounts[m].backend == BACKEND_MINIFS) {
        return fs_read_file(rest, buf, max_len);
    }
    if (g_mounts[m].backend == BACKEND_DEVICE) {
        return device_read(rest, buf, max_len);
    }
    return -1;
}

// Device pseudo-files are read-only; only a MiniFS-backed mount accepts writes.
bool vfs_write(const char* path, u8* data, u32 len) {
    int m = vfs_find_mount(path);
    if (m < 0) {
        return false;
    }
    const char* rest = vfs_strip_prefix(path, m);
    if (g_mounts[m].backend == BACKEND_MINIFS) {
        return fs_write_file(rest, data, len);
    }
    return false;
}
