// Path-namespace VFS: routes a path to whichever backend's mount prefix matches
// (`/system` -> MiniFS, `/devices` -> devfs), tag + if/else dispatch.

#include "vfs.h"
#include "../minifs/minifs.h"
#include "../devfs/devfs.h"
#include "../procfs/procfs.h"
#include "../tmpfs/tmpfs.h"
#include "../fat32/fat32.h"
#include "../../lib/strings.h"

const u32 BACKEND_MINIFS = 1;
const u32 BACKEND_DEVICE = 2;
const u32 BACKEND_PROCFS = 3;
const u32 BACKEND_TMPFS = 4;
const u32 BACKEND_MOUNTS = 5;
const u32 BACKEND_FAT32 = 6;

typedef struct {
    char prefix[16];
    u32 backend;
    char backend_root[16];  // "" = the backend's own root (every mount before this existed)
    bool used;
} mount;

// 8: system/devices/processes/tmp/volumes/apps/users/fat32 - the real
// Faza I point 6 mount set.
static mount g_mounts[8];
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

bool vfs_mount_at(const char* prefix, u32 backend, const char* backend_root) {
    if (g_mount_count >= 8) {
        return false;
    }
    copy_prefix(g_mounts[g_mount_count].prefix, prefix);
    g_mounts[g_mount_count].backend = backend;
    copy_prefix(g_mounts[g_mount_count].backend_root, backend_root);
    g_mounts[g_mount_count].used = true;
    g_mount_count = g_mount_count + 1;
    return true;
}

bool vfs_mount(const char* prefix, u32 backend) {
    return vfs_mount_at(prefix, backend, "");
}

// Combines a mount's backend_root with the already-VFS-prefix-stripped
// rest of the path, e.g. root "apps" + rest "foo.bin" -> "apps/foo.bin";
// root "apps" + rest "" -> "apps" (listing the mount's own root); root
// "" + rest "foo.bin" -> "foo.bin" (every pre-existing mount, unchanged).
// minifs.c's resolve_parent_dir/resolve_dir already walk multi-component
// paths, so no MiniFS-side change is needed for this.
static void combine_backend_path(char* out, const char* root, const char* rest) {
    int i = 0;
    int j = 0;
    while (root[j] != '\0') {
        out[i] = root[j];
        i = i + 1;
        j = j + 1;
    }
    if (root[0] != '\0' && rest[0] != '\0') {
        out[i] = '/';
        i = i + 1;
    }
    j = 0;
    while (rest[j] != '\0') {
        out[i] = rest[j];
        i = i + 1;
        j = j + 1;
    }
    out[i] = '\0';
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

int vfs_read(const char* path, u8* buf, u32 max_len, u8 caller_uid) {
    int m = vfs_find_mount(path);
    if (m < 0) {
        return -1;
    }
    const char* rest = vfs_strip_prefix(path, m);
    if (g_mounts[m].backend == BACKEND_MINIFS) {
        char full[200];
        combine_backend_path(full, g_mounts[m].backend_root, rest);
        u8 owner_uid;
        u8 mode;
        if (fs_get_owner_mode(full, &owner_uid, &mode)) {
            if ((mode & MODE_OWNER_ONLY_READ) != 0 && caller_uid != owner_uid && caller_uid != 0) {
                return -1;
            }
        }
        return fs_read_file(full, buf, max_len);
    }
    if (g_mounts[m].backend == BACKEND_DEVICE) {
        return device_read(rest, buf, max_len);
    }
    if (g_mounts[m].backend == BACKEND_PROCFS) {
        return procfs_read(rest, buf, max_len);
    }
    if (g_mounts[m].backend == BACKEND_TMPFS) {
        return tmpfs_read(rest, buf, max_len);
    }
    if (g_mounts[m].backend == BACKEND_FAT32) {
        return fat32_read_file(rest, buf, max_len);
    }
    return -1;
}

// Device/process/mount-listing pseudo-files are read-only; MiniFS- and
// tmpfs-backed mounts accept writes.
bool vfs_write(const char* path, u8* data, u32 len, u8 caller_uid) {
    int m = vfs_find_mount(path);
    if (m < 0) {
        return false;
    }
    const char* rest = vfs_strip_prefix(path, m);
    if (g_mounts[m].backend == BACKEND_MINIFS) {
        char full[200];
        combine_backend_path(full, g_mounts[m].backend_root, rest);
        u8 owner_uid;
        u8 mode;
        if (fs_get_owner_mode(full, &owner_uid, &mode)) {
            if ((mode & MODE_OWNER_ONLY_WRITE) != 0 && caller_uid != owner_uid && caller_uid != 0) {
                return false;
            }
        }
        return fs_write_file(full, data, len);
    }
    if (g_mounts[m].backend == BACKEND_TMPFS) {
        return tmpfs_write(rest, data, len);
    }
    if (g_mounts[m].backend == BACKEND_FAT32) {
        return fat32_write_file(rest, data, len);
    }
    return false;
}

// Lists the registered mount points themselves as directories (their
// own prefix, minus the leading '/') - the real VFS root's own listing
// (dir_path == ""), and (Faza I point 6, item 12) literally the same
// content /volumes shows (BACKEND_MOUNTS below) - one real listing, two
// places it's reachable from.
static bool list_mount_entries(int index, char* name_out, u32* size_out, bool* is_dir_out) {
    if (index < 0 || index >= g_mount_count || !g_mounts[index].used) {
        return false;
    }
    int i = 0;
    while (g_mounts[index].prefix[i + 1] != '\0') {  // +1 skips the leading '/'
        name_out[i] = g_mounts[index].prefix[i + 1];
        i = i + 1;
    }
    name_out[i] = '\0';
    *size_out = 0;
    *is_dir_out = true;
    return true;
}

// dir_path == "" is the real VFS root - see list_mount_entries() above.
// Otherwise resolve to the owning mount and delegate to that backend's
// own listing.
bool vfs_list_entry(const char* dir_path, int index, char* name_out, u32* size_out, bool* is_dir_out) {
    if (dir_path[0] == '\0') {
        return list_mount_entries(index, name_out, size_out, is_dir_out);
    }

    int m = vfs_find_mount(dir_path);
    if (m < 0) {
        return false;
    }
    const char* rest = vfs_strip_prefix(dir_path, m);
    if (g_mounts[m].backend == BACKEND_MINIFS) {
        char full[200];
        combine_backend_path(full, g_mounts[m].backend_root, rest);
        return fs_list_entry(full, index, name_out, size_out, is_dir_out);
    }
    if (g_mounts[m].backend == BACKEND_DEVICE) {
        return devfs_list_entry(rest, index, name_out, size_out, is_dir_out);
    }
    if (g_mounts[m].backend == BACKEND_PROCFS) {
        return procfs_list_entry(index, name_out, size_out, is_dir_out);
    }
    if (g_mounts[m].backend == BACKEND_TMPFS) {
        return tmpfs_list_entry(index, name_out, size_out, is_dir_out);
    }
    if (g_mounts[m].backend == BACKEND_MOUNTS) {
        return list_mount_entries(index, name_out, size_out, is_dir_out);
    }
    if (g_mounts[m].backend == BACKEND_FAT32) {
        return fat32_list_entry(rest, index, name_out, size_out, is_dir_out);
    }
    return false;
}

bool vfs_stat(const char* path, u32* size_out, bool* is_dir_out, u8* owner_uid_out, u8* mode_out) {
    int m = vfs_find_mount(path);
    if (m < 0) {
        return false;
    }
    const char* rest = vfs_strip_prefix(path, m);
    if (g_mounts[m].backend == BACKEND_MINIFS) {
        char full[200];
        combine_backend_path(full, g_mounts[m].backend_root, rest);
        return fs_stat_file(full, size_out, is_dir_out, owner_uid_out, mode_out);
    }
    if (g_mounts[m].backend == BACKEND_TMPFS) {
        // No owner/mode concept on tmpfs - real defaults, not fabricated
        // permission state.
        *owner_uid_out = 0;
        *mode_out = 0;
        return tmpfs_stat(rest, size_out, is_dir_out);
    }
    if (g_mounts[m].backend == BACKEND_FAT32) {
        // No owner/mode concept on real FAT32 either - same honest
        // defaults as tmpfs above.
        *owner_uid_out = 0;
        *mode_out = 0;
        return fat32_stat_file(rest, size_out, is_dir_out);
    }
    return false;
}

bool vfs_delete(const char* path, u8 caller_uid) {
    int m = vfs_find_mount(path);
    if (m < 0) {
        return false;
    }
    const char* rest = vfs_strip_prefix(path, m);
    if (g_mounts[m].backend == BACKEND_MINIFS) {
        char full[200];
        combine_backend_path(full, g_mounts[m].backend_root, rest);
        u8 owner_uid;
        u8 mode;
        if (fs_get_owner_mode(full, &owner_uid, &mode)) {
            if ((mode & MODE_OWNER_ONLY_WRITE) != 0 && caller_uid != owner_uid && caller_uid != 0) {
                return false;
            }
        }
        return fs_delete_file(full);
    }
    if (g_mounts[m].backend == BACKEND_TMPFS) {
        return tmpfs_delete(rest);
    }
    if (g_mounts[m].backend == BACKEND_FAT32) {
        return fat32_delete_file(rest);
    }
    return false;
}

bool vfs_mkdir(const char* path) {
    int m = vfs_find_mount(path);
    if (m < 0) {
        return false;
    }
    const char* rest = vfs_strip_prefix(path, m);
    if (g_mounts[m].backend == BACKEND_MINIFS) {
        char full[200];
        combine_backend_path(full, g_mounts[m].backend_root, rest);
        return fs_create_dir(full);
    }
    if (g_mounts[m].backend == BACKEND_FAT32) {
        return fat32_create_dir(rest);
    }
    return false;
}

bool vfs_is_writable(const char* path) {
    int m = vfs_find_mount(path);
    if (m < 0) {
        return false;
    }
    return g_mounts[m].backend == BACKEND_MINIFS || g_mounts[m].backend == BACKEND_TMPFS
        || g_mounts[m].backend == BACKEND_FAT32;
}

bool vfs_resolve_minifs_path(const char* path, char* out) {
    int m = vfs_find_mount(path);
    if (m < 0 || g_mounts[m].backend != BACKEND_MINIFS) {
        return false;
    }
    const char* rest = vfs_strip_prefix(path, m);
    combine_backend_path(out, g_mounts[m].backend_root, rest);
    return true;
}
