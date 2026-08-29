// VFS backend for /processes: not disk-backed, reflects live kernel state
// (proc/process.h's g_processes[]) - same idea as devfs's own /devices,
// just backed by the process table instead of a hardcoded pseudo-file.

#include "procfs.h"
#include "../../../proc/process.h"
#include "../../lib/strings.h"

static void write_name(char* out, int index) {
    out[0] = 'p'; out[1] = 'r'; out[2] = 'o'; out[3] = 'c';
    // MAX_PROCESSES is small (single-digit today) - a real multi-digit
    // formatter would be needed if that ever grows past 9, same
    // reasoning print_decimal already documents for its own callers.
    out[4] = (char) ('0' + (index % 10));
    out[5] = '\0';
}

bool procfs_list_entry(int index, char* name_out, u32* size_out, bool* is_dir_out) {
    if (index < 0 || index >= MAX_PROCESSES || !g_processes[index].used) {
        return false;
    }
    write_name(name_out, index);
    *size_out = 0;
    *is_dir_out = false;
    return true;
}

int procfs_read(const char* name, u8* buf, u32 max_len) {
    if (!starts_with(name, "proc")) {
        return -1;
    }
    int index = name[4] - '0';
    if (index < 0 || index >= MAX_PROCESSES || !g_processes[index].used) {
        return -1;
    }

    const char* prefix = "task=0x";
    int i = 0;
    while (prefix[i] != '\0') {
        if ((u32) i >= max_len) {
            return -1;
        }
        buf[i] = (u8) prefix[i];
        i = i + 1;
    }
    int len = format_hex((u64) g_processes[index].task_index, &buf[i]);
    i = i + len;

    const char* mid = " cr3=0x";
    int j = 0;
    while (mid[j] != '\0') {
        if ((u32) i >= max_len) {
            return -1;
        }
        buf[i] = (u8) mid[j];
        i = i + 1;
        j = j + 1;
    }
    len = format_hex(g_processes[index].cr3, &buf[i]);
    i = i + len;

    if ((u32) i >= max_len) {
        return -1;
    }
    buf[i] = 0;
    return i;
}
