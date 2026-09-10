#include "directory.h"
#include "../../../kernel/fs/vfs/vfs.h"

open_directory g_open_directories[DIRECTORY_SLOTS];

static int find_free_slot(void) {
    int i = 0;
    while (i < DIRECTORY_SLOTS) {
        if (!g_open_directories[i].used) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

int directory_object_open(const char* dir_path) {
    int slot = find_free_slot();
    if (slot < 0) {
        return -1;
    }
    open_directory* d = &g_open_directories[slot];
    int i = 0;
    while (i < 127 && dir_path[i] != '\0') {
        d->dir_path[i] = dir_path[i];
        i = i + 1;
    }
    d->dir_path[i] = '\0';
    d->cursor = 0;
    d->used = true;
    return slot;
}

bool directory_object_read_next(int slot, char* name_out, u32* size_out, bool* is_dir_out) {
    open_directory* d = &g_open_directories[slot];
    bool ok = vfs_list_entry(d->dir_path, d->cursor, name_out, size_out, is_dir_out);
    if (ok) {
        d->cursor = d->cursor + 1;
    }
    return ok;
}

bool directory_object_close(int slot) {
    g_open_directories[slot].used = false;
    return true;
}
