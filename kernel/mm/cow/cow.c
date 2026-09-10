#include "cow.h"

cow_entry g_cow_frames[COW_SLOTS];

static int find_slot(void* frame) {
    int i = 0;
    while (i < COW_SLOTS) {
        if (g_cow_frames[i].used && g_cow_frames[i].frame == frame) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

bool cow_track(void* frame) {
    int slot = find_slot(frame);
    if (slot >= 0) {
        g_cow_frames[slot].refcount = g_cow_frames[slot].refcount + 1;
        return true;
    }
    int i = 0;
    while (i < COW_SLOTS) {
        if (!g_cow_frames[i].used) {
            g_cow_frames[i].used = true;
            g_cow_frames[i].frame = frame;
            g_cow_frames[i].refcount = 2;
            return true;
        }
        i = i + 1;
    }
    return false;
}

bool cow_is_shared(void* frame) {
    return find_slot(frame) >= 0;
}

bool cow_should_free(void* frame) {
    int slot = find_slot(frame);
    if (slot < 0) {
        return true;  // never COW-tracked - a normal, exclusively-owned frame
    }
    g_cow_frames[slot].refcount = g_cow_frames[slot].refcount - 1;
    if (g_cow_frames[slot].refcount <= 0) {
        g_cow_frames[slot].used = false;
        return true;
    }
    return false;
}
