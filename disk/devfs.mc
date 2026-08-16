// Milestone 18's second VFS backend - deliberately not disk-backed at
// all. Every "file" under /devices reflects live kernel state, composed
// into the caller's buffer on the spot rather than read off a block
// device - the same idea as real Unix's /proc or /dev, minimal version.
// Existing only to give the VFS layer a genuinely different backend to
// route to, proving `vfsRead()` really dispatches rather than just
// being MiniFS with an extra path prefix stripped off first.

import "../isr/isr.mc";
import "../lib/strings.mc";

// Only one pseudo-file exists so far ("ticks", i.e. /devices/ticks) -
// more (task count, free heap, free frames, ...) are straightforward
// additions of the same shape whenever something needs to read kernel
// state through the VFS instead of a dedicated shell command.
int deviceRead(char* name, u8* buf, u32 maxLen) {
    if (streq(name, "ticks")) {
        char* prefix = "ticks: 0x";
        int i = 0;
        while (prefix[i] != '\0') {
            if ((u32) i >= maxLen) {
                return -1;
            }
            buf[i] = (u8) prefix[i];
            i = i + 1;
        }
        int hexLen = formatHex(gTickCount, &buf[i]);
        i = i + hexLen;
        if ((u32) i >= maxLen) {
            return -1;
        }
        buf[i] = 0;
        return i;
    }
    return -1;
}
