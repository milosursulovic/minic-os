// A second VFS backend - deliberately not disk-backed at all. Every
// "file" under /devices reflects live kernel state, composed into the
// caller's buffer on the spot rather than read off a block device - the
// same idea as real Unix's /proc or /dev, minimal version.

#include "devfs.h"
#include "../isr/isr.h"
#include "../lib/strings.h"

// Only one pseudo-file exists so far ("ticks", i.e. /devices/ticks) -
// more (task count, free heap, free frames, ...) are straightforward
// additions of the same shape whenever something needs to read kernel
// state through the VFS instead of a dedicated shell command.
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
