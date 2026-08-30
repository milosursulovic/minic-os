#include "file.h"
#include "../../../kernel/fs/vfs/vfs.h"
#include "../../../kernel/fs/minifs/minifs.h"
#include "../../../kernel/lib/strings.h"

open_file g_open_files[FILE_OBJECT_SLOTS];

// fs_delete_file (raw MiniFS, no VFS wrapper) is only meaningful inside
// the one writable mount - same in_system_mount()/strip_system_prefix()
// logic shell/shell/shell.c and proc/apps/file_manager/file_manager.c
// each already independently carry (can't share source across the
// ring0/ring3 boundary, and this is yet another separate translation
// unit), applied here for a write-mode file object's real overwrite.
static bool in_system_mount(const char* path) {
    return starts_with(path, "/system");
}

static void strip_system_prefix(char* out, const char* path) {
    const char* rest = &path[7];  // strlen("/system")
    if (rest[0] == '/') {
        rest = &rest[1];
    }
    int i = 0;
    while (rest[i] != '\0') {
        out[i] = rest[i];
        i = i + 1;
    }
    out[i] = '\0';
}

static int find_free_slot(void) {
    int i = 0;
    while (i < FILE_OBJECT_SLOTS) {
        if (!g_open_files[i].used) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

int file_object_open(const char* path, bool write_mode, u8 caller_uid) {
    if (in_system_mount(path)) {
        char stripped[128];
        strip_system_prefix(stripped, path);
        u8 owner_uid;
        u8 mode;
        if (fs_get_owner_mode(stripped, &owner_uid, &mode)) {
            u8 restriction = write_mode ? MODE_OWNER_ONLY_WRITE : MODE_OWNER_ONLY_READ;
            if ((mode & restriction) != 0 && caller_uid != owner_uid && caller_uid != 0) {
                return -1;
            }
        }
        // fs_get_owner_mode() returning false means the path doesn't
        // exist yet - nothing to check permissions against (a write-mode
        // open is about to create it; a read-mode open will fail below
        // exactly as it always has).
    }

    int slot = find_free_slot();
    if (slot < 0) {
        return -1;
    }
    open_file* f = &g_open_files[slot];

    int i = 0;
    while (i < 127 && path[i] != '\0') {
        f->path[i] = path[i];
        i = i + 1;
    }
    f->path[i] = '\0';

    f->write_mode = write_mode;
    f->cursor = 0;
    f->owner_uid = caller_uid;
    if (write_mode) {
        f->length = 0;
    } else {
        int n = vfs_read(path, f->buffer, FILE_MAX_SIZE);
        if (n < 0) {
            return -1;
        }
        f->length = (u32) n;
    }
    f->used = true;
    return slot;
}

int file_object_read(int slot, u8* out, u32 max_len) {
    open_file* f = &g_open_files[slot];
    u32 remaining = f->length - f->cursor;
    u32 n = max_len < remaining ? max_len : remaining;
    u32 i = 0;
    while (i < n) {
        out[i] = f->buffer[f->cursor + i];
        i = i + 1;
    }
    f->cursor = f->cursor + n;
    return (int) n;
}

int file_object_write(int slot, const u8* data, u32 len) {
    open_file* f = &g_open_files[slot];
    u32 space = FILE_MAX_SIZE - f->length;
    u32 n = len < space ? len : space;
    u32 i = 0;
    while (i < n) {
        f->buffer[f->length + i] = data[i];
        i = i + 1;
    }
    f->length = f->length + n;
    return (int) n;
}

bool file_object_seek(int slot, u32 pos) {
    open_file* f = &g_open_files[slot];
    if (f->write_mode) {
        return false;
    }
    if (pos > f->length) {
        return false;
    }
    f->cursor = pos;
    return true;
}

bool file_object_close(int slot) {
    open_file* f = &g_open_files[slot];
    bool ok = true;
    if (f->write_mode) {
        bool is_system = in_system_mount(f->path);
        char stripped[128];
        if (is_system) {
            strip_system_prefix(stripped, f->path);
            fs_delete_file(stripped);  // overwrite semantics - ok if it didn't exist yet
        }
        ok = vfs_write(f->path, f->buffer, f->length);
        if (ok && is_system) {
            // Real Unix "creator becomes owner" - applied for real once
            // the file genuinely exists on disk, not at open() time.
            fs_set_owner(stripped, f->owner_uid);
        }
    }
    f->used = false;
    return ok;
}
