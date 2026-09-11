// Real ephemeral RAM-backed /temp - see tmpfs.h for the design story.

#include "tmpfs.h"
#include "../../lib/strings.h"

typedef struct {
    bool used;
    char name[20];  // same cap as minifs's dir_entry.name - callers (e.g.
                     // shell/shell/commands/fs.c's cmd_ls) size their own
                     // name_out buffers against that existing convention.
    u8 buffer[TMPFS_MAX_SIZE];
    u32 size;
} tmpfs_entry;

static tmpfs_entry g_tmpfs_entries[TMPFS_SLOTS];

static int find_entry(const char* name) {
    int i = 0;
    while (i < TMPFS_SLOTS) {
        if (g_tmpfs_entries[i].used && streq(g_tmpfs_entries[i].name, name)) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

static void copy_name(char* dst, const char* src) {
    int i = 0;
    while (i < 19 && src[i] != '\0') {
        dst[i] = src[i];
        i = i + 1;
    }
    dst[i] = '\0';
}

int tmpfs_read(const char* name, u8* buf, u32 max_len) {
    int slot = find_entry(name);
    if (slot < 0) {
        return -1;
    }
    tmpfs_entry* e = &g_tmpfs_entries[slot];
    if (e->size > max_len) {
        return -2;
    }
    u32 i = 0;
    while (i < e->size) {
        buf[i] = e->buffer[i];
        i = i + 1;
    }
    return (int) e->size;
}

bool tmpfs_write(const char* name, u8* data, u32 len) {
    if (len > TMPFS_MAX_SIZE) {
        return false;
    }
    int slot = find_entry(name);
    if (slot < 0) {
        int i = 0;
        while (i < TMPFS_SLOTS) {
            if (!g_tmpfs_entries[i].used) {
                slot = i;
                break;
            }
            i = i + 1;
        }
        if (slot < 0) {
            return false;
        }
        copy_name(g_tmpfs_entries[slot].name, name);
        g_tmpfs_entries[slot].used = true;
    }
    tmpfs_entry* e = &g_tmpfs_entries[slot];
    u32 i = 0;
    while (i < len) {
        e->buffer[i] = data[i];
        i = i + 1;
    }
    e->size = len;
    return true;
}

bool tmpfs_delete(const char* name) {
    int slot = find_entry(name);
    if (slot < 0) {
        return false;
    }
    g_tmpfs_entries[slot].used = false;
    return true;
}

bool tmpfs_list_entry(int index, char* name_out, u32* size_out, bool* is_dir_out) {
    if (index < 0 || index >= TMPFS_SLOTS || !g_tmpfs_entries[index].used) {
        return false;
    }
    copy_name(name_out, g_tmpfs_entries[index].name);
    *size_out = g_tmpfs_entries[index].size;
    *is_dir_out = false;
    return true;
}

bool tmpfs_stat(const char* name, u32* size_out, bool* is_dir_out) {
    int slot = find_entry(name);
    if (slot < 0) {
        return false;
    }
    *size_out = g_tmpfs_entries[slot].size;
    *is_dir_out = false;
    return true;
}
