// Milestone 18: a VFS abstraction over milestone 17's MiniFS - the last
// step of the storage phase. Before this, the shell called
// fs_read_file/fs_write_file directly; there was no path namespace, no way
// to route a request to more than one backend, and no way to plug in a
// second filesystem implementation even in principle. A basic namespace
// (`/system`, `/devices`, ...) fixes that: `vfs_read`/`vfs_write` take a
// path, find which mount prefix it falls under, strip the prefix, and
// dispatch to whichever backend owns that mount - tag + if/else, the
// same dispatch style `proc/object.mc`'s KernelObject.type already
// established in this kernel, not function pointers (untested in this
// codebase's freestanding/no-register-allocation constraints, and this
// milestone's actual point is proving routing works, not exercising a
// different MiniC feature).
//
// The real proof this is a genuine abstraction, not just a renamed API:
// two backends exist from day one. `/system` routes to MiniFS (real
// disk I/O). `/devices` (disk/devfs.mc) routes to live kernel state -
// nothing touches the disk at all. The exact same `vfs_read()` call
// reaching two completely different mechanisms depending only on the
// path prefix is what "VFS" actually means.

import "minifs.mc";
import "devfs.mc";
import "../lib/strings.mc";

u32 backend_minifs = 1;
u32 backend_device = 2;

struct mount {
    char prefix[16];
    u32 backend;
    bool used;
}

mount g_mounts[4];
int g_mount_count;

void copy_prefix(char* dst, char* src) {
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

bool vfs_mount(char* prefix, u32 backend) {
    if (g_mount_count >= 4) {
        return false;
    }
    copy_prefix(g_mounts[g_mount_count].prefix, prefix);
    g_mounts[g_mount_count].backend = backend;
    g_mounts[g_mount_count].used = true;
    g_mount_count = g_mount_count + 1;
    return true;
}

int vfs_find_mount(char* path) {
    int i = 0;
    while (i < g_mount_count) {
        if (g_mounts[i].used && starts_with(path, g_mounts[i].prefix)) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

// Strips a matched mount's prefix (and the separating '/', if present)
// off a path - "/system/hello.txt" through the "/system" mount becomes
// "hello.txt", exactly the bare name MiniFS's own API expects.
char* vfs_strip_prefix(char* path, int mount_index) {
    int prefix_len = strlen(g_mounts[mount_index].prefix);
    char* rest = &path[prefix_len];
    if (rest[0] == '/') {
        rest = &rest[1];
    }
    return rest;
}

int vfs_read(char* path, u8* buf, u32 max_len) {
    int m = vfs_find_mount(path);
    if (m < 0) {
        return -1;
    }
    char* rest = vfs_strip_prefix(path, m);
    if (g_mounts[m].backend == backend_minifs) {
        return fs_read_file(rest, buf, max_len);
    }
    if (g_mounts[m].backend == backend_device) {
        return device_read(rest, buf, max_len);
    }
    return -1;
}

// Device pseudo-files are read-only (they reflect live kernel state -
// there's nothing meaningful to write back into g_tick_count by writing
// "/devices/ticks"), so only a MiniFS-backed mount ever accepts a write.
bool vfs_write(char* path, u8* data, u32 len) {
    int m = vfs_find_mount(path);
    if (m < 0) {
        return false;
    }
    char* rest = vfs_strip_prefix(path, m);
    if (g_mounts[m].backend == backend_minifs) {
        return fs_write_file(rest, data, len);
    }
    return false;
}
