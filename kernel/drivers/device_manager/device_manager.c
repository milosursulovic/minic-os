#include "device_manager.h"

device_entry g_devices[MAX_DEVICES];
int g_device_count;

static bool name_matches(const char* a, const char* b) {
    int i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return false;
        }
        i = i + 1;
    }
    return a[i] == b[i];
}

static void copy_bounded(char* dst, const char* src, int cap) {
    int i = 0;
    while (i < cap - 1 && src[i] != '\0') {
        dst[i] = src[i];
        i = i + 1;
    }
    dst[i] = '\0';
}

static int find_existing(const char* name, int category) {
    int i = 0;
    while (i < MAX_DEVICES) {
        if (g_devices[i].used && g_devices[i].category == category && name_matches(g_devices[i].name, name)) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

int device_manager_register(const char* name, int category, u32 info) {
    int existing = find_existing(name, category);
    if (existing >= 0) {
        g_devices[existing].info = info;
        return existing;
    }
    int i = 0;
    while (i < MAX_DEVICES) {
        if (!g_devices[i].used) {
            copy_bounded(g_devices[i].name, name, 32);
            g_devices[i].category = category;
            g_devices[i].info = info;
            g_devices[i].used = true;
            g_device_count = g_device_count + 1;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

bool device_manager_get(int index, char* name_out, int* category_out, u32* info_out) {
    if (index < 0 || index >= MAX_DEVICES || !g_devices[index].used) {
        return false;
    }
    copy_bounded(name_out, g_devices[index].name, 32);
    *category_out = g_devices[index].category;
    *info_out = g_devices[index].info;
    return true;
}
