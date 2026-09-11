// VFS backend for /devices: not disk-backed, reflects live kernel state
// (like /processes' procfs). Faza I point 9, item 13: a real nested
// tree over kernel/drivers/device_manager's existing categorized
// registry, instead of the one flat "ticks" pseudo-file this used to be
// the whole of.

#include "devfs.h"
#include "../../isr/isr.h"
#include "../../drivers/device_manager/device_manager.h"
#include "../../lib/strings.h"

// Two already-registered device names contain a literal '/'
// ("PS/2 Keyboard"/"PS/2 Mouse" - kmain.c/mouse.c), which would silently
// break path parsing if exposed verbatim as a devfs pseudo-file name
// (a path component can't itself contain '/'). Replaces it with '_' for
// the filesystem-visible name only - device_manager's own stored name
// (shown by the console `devices` command/GUI app) is untouched. Used
// by both list and read so they never disagree on what a name looks
// like.
static void sanitize_name(char* out, const char* raw) {
    int i = 0;
    while (raw[i] != '\0' && i < 31) {
        out[i] = raw[i] == '/' ? '_' : raw[i];
        i = i + 1;
    }
    out[i] = '\0';
}

static bool category_for_subpath(const char* subpath, int* category_out) {
    if (streq(subpath, "pci")) {
        *category_out = DEVICE_CATEGORY_PCI;
        return true;
    }
    if (streq(subpath, "platform")) {
        *category_out = DEVICE_CATEGORY_PLATFORM;
        return true;
    }
    if (streq(subpath, "input")) {
        *category_out = DEVICE_CATEGORY_INPUT;
        return true;
    }
    return false;
}

// Only "ticks" and "<category>/<sanitized device name>" exist.
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

    // Split "category/devicename" - the first '/' separates them.
    int slash = -1;
    int k = 0;
    while (name[k] != '\0') {
        if (name[k] == '/') {
            slash = k;
            break;
        }
        k = k + 1;
    }
    if (slash < 0) {
        return -1;
    }
    char category_str[16];
    int j = 0;
    while (j < slash && j < 15) {
        category_str[j] = name[j];
        j = j + 1;
    }
    category_str[j] = '\0';
    const char* wanted_name = &name[slash + 1];

    int category;
    if (!category_for_subpath(category_str, &category)) {
        return -1;
    }

    int idx = 0;
    char raw_name[32];
    u32 info;
    while (device_manager_get_in_category(category, idx, raw_name, &info)) {
        char sanitized[32];
        sanitize_name(sanitized, raw_name);
        if (streq(sanitized, wanted_name)) {
            int i = 0;
            if (category == DEVICE_CATEGORY_PCI) {
                const char* prefix = "vendor=0x";
                while (prefix[i] != '\0') {
                    if ((u32) i >= max_len) {
                        return -1;
                    }
                    buf[i] = (u8) prefix[i];
                    i = i + 1;
                }
                i = i + format_hex((u64) (info >> 16), &buf[i]);
                const char* mid = " device=0x";
                int m = 0;
                while (mid[m] != '\0') {
                    if ((u32) i >= max_len) {
                        return -1;
                    }
                    buf[i] = (u8) mid[m];
                    i = i + 1;
                    m = m + 1;
                }
                i = i + format_hex((u64) (info & 0xFFFF), &buf[i]);
            } else if (category == DEVICE_CATEGORY_INPUT) {
                const char* prefix = "irq=0x";
                while (prefix[i] != '\0') {
                    if ((u32) i >= max_len) {
                        return -1;
                    }
                    buf[i] = (u8) prefix[i];
                    i = i + 1;
                }
                i = i + format_hex((u64) info, &buf[i]);
            } else {
                // PLATFORM has no real info concept yet (kmain.c/vbe.c
                // both register it as 0) - an honest label, not a fake
                // number.
                const char* text = "platform device";
                while (text[i] != '\0') {
                    if ((u32) i >= max_len) {
                        return -1;
                    }
                    buf[i] = (u8) text[i];
                    i = i + 1;
                }
            }
            if ((u32) i >= max_len) {
                return -1;
            }
            buf[i] = 0;
            return i;
        }
        idx = idx + 1;
    }
    return -1;
}

bool devfs_list_entry(const char* subpath, int index, char* name_out, u32* size_out, bool* is_dir_out) {
    if (subpath[0] == '\0') {
        if (index == 0) {
            name_out[0] = 't'; name_out[1] = 'i'; name_out[2] = 'c';
            name_out[3] = 'k'; name_out[4] = 's'; name_out[5] = '\0';
            *size_out = 0;
            *is_dir_out = false;
            return true;
        }
        const char* categories[3] = { "pci", "platform", "input" };
        if (index >= 1 && index <= 3) {
            const char* cat_name = categories[index - 1];
            int i = 0;
            while (cat_name[i] != '\0') {
                name_out[i] = cat_name[i];
                i = i + 1;
            }
            name_out[i] = '\0';
            *size_out = 0;
            *is_dir_out = true;
            return true;
        }
        return false;
    }

    int category;
    if (!category_for_subpath(subpath, &category)) {
        return false;
    }
    char raw_name[32];
    u32 info;
    if (!device_manager_get_in_category(category, index, raw_name, &info)) {
        return false;
    }
    sanitize_name(name_out, raw_name);
    *size_out = 0;
    *is_dir_out = false;
    return true;
}
