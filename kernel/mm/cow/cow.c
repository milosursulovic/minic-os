#include "cow.h"

cow_entry g_cow_frames[COW_SLOTS];

// Save/restore IF - same established pattern kernel/mm/frames/frames.c's
// alloc_frame()/kernel/mm/heap/heap.c's kalloc() use, for the identical
// bug class ([[project_mouse_keyboard_race_bug]]): find_slot()-then-
// mutate with no atomicity. cow_track() is called from
// clone_address_space_cow() (a fork() in progress, walking the parent's
// page tables) while cow_is_shared()/cow_should_free() are also called
// from the page-fault handler's own COW-write repair and from
// free_address_space() on process exit - a preempting timer tick handing
// control to a completely different task's own COW fault or exit mid-way
// through one of these calls could read a slot's used/frame/refcount
// fields half-written, misjudging whether a frame is still shared and
// free_frame()-ing one that another live process still maps.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

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
    u64 saved_flags = disable_interrupts();
    int slot = find_slot(frame);
    if (slot >= 0) {
        g_cow_frames[slot].refcount = g_cow_frames[slot].refcount + 1;
        restore_interrupts(saved_flags);
        return true;
    }
    int i = 0;
    while (i < COW_SLOTS) {
        if (!g_cow_frames[i].used) {
            g_cow_frames[i].used = true;
            g_cow_frames[i].frame = frame;
            g_cow_frames[i].refcount = 2;
            restore_interrupts(saved_flags);
            return true;
        }
        i = i + 1;
    }
    restore_interrupts(saved_flags);
    return false;
}

bool cow_is_shared(void* frame) {
    u64 saved_flags = disable_interrupts();
    bool shared = find_slot(frame) >= 0;
    restore_interrupts(saved_flags);
    return shared;
}

bool cow_should_free(void* frame) {
    u64 saved_flags = disable_interrupts();
    int slot = find_slot(frame);
    if (slot < 0) {
        restore_interrupts(saved_flags);
        return true;  // never COW-tracked - a normal, exclusively-owned frame
    }
    g_cow_frames[slot].refcount = g_cow_frames[slot].refcount - 1;
    if (g_cow_frames[slot].refcount <= 0) {
        g_cow_frames[slot].used = false;
        restore_interrupts(saved_flags);
        return true;
    }
    restore_interrupts(saved_flags);
    return false;
}
