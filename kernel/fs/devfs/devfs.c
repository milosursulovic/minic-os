// VFS backend for /devices: not disk-backed, reflects live kernel state (like /proc).

#include "devfs.h"
#include "../../isr/isr.h"
#include "../../lib/strings.h"

// Only "ticks" exists so far.
int device_read(const char* name, u8* buf, u32 max_len) {
    if (streq(name, "ticks")) {
        const char* prefix = "ticks: 0x";
        int i = 0;
        while (prefix[i] != '\0') {
            if ((u32) i >= max_len) {
                return -1;
            }
            buf[i] = (u8) prefix[i];
            i = i + 1;
        }
        int hex_len = format_hex(g_tick_count, &buf[i]);
        i = i + hex_len;
        if ((u32) i >= max_len) {
            return -1;
        }
        buf[i] = 0;
        return i;
    }
    return -1;
}

bool devfs_list_entry(int index, char* name_out, u32* size_out, bool* is_dir_out) {
    if (index != 0) {
        return false;
    }
    name_out[0] = 't'; name_out[1] = 'i'; name_out[2] = 'c';
    name_out[3] = 'k'; name_out[4] = 's'; name_out[5] = '\0';
    *size_out = 0;
    *is_dir_out = false;
    return true;
}
