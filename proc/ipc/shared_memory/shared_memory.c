#include "shared_memory.h"
#include "../../../kernel/mm/frames/frames.h"
#include "../../../kernel/mm/paging/paging.h"

shared_region g_shared_regions[SHM_SLOTS];

// Save/restore IF - same established pattern kernel/mm/frames/frames.c's
// alloc_frame() uses, for the identical bug class
// ([[project_mouse_keyboard_race_bug]]): find_free_slot()-then-mutate
// with no atomicity, so two callers racing a preemption in between could
// both claim the same slot.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

static int find_free_slot(void) {
    int i = 0;
    while (i < SHM_SLOTS) {
        if (!g_shared_regions[i].used) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

int alloc_shared_memory(u32 size) {
    u32 page_count = (size + 4095) / 4096;
    if (page_count == 0 || page_count > SHM_MAX_PAGES) {
        return -1;
    }
    u64 saved_flags = disable_interrupts();
    int slot = find_free_slot();
    if (slot < 0) {
        restore_interrupts(saved_flags);
        return -1;
    }
    g_shared_regions[slot].used = true;  // claim now, before releasing IF - fill in below

    u32 p = 0;
    while (p < page_count) {
        void* frame = alloc_frame();
        if (frame == NULL) {
            g_shared_regions[slot].used = false;
            restore_interrupts(saved_flags);
            return -1;
        }
        g_shared_regions[slot].frames[p] = frame;
        p = p + 1;
    }
    g_shared_regions[slot].page_count = page_count;
    restore_interrupts(saved_flags);
    return slot;
}

bool shared_memory_map(int index, u64 cr3, u64 vaddr) {
    shared_region* r = &g_shared_regions[index];
    u32 p = 0;
    while (p < r->page_count) {
        if (!map_page_in(cr3, vaddr + (u64) p * 4096, (u64) r->frames[p], 0x06)) {
            return false;
        }
        p = p + 1;
    }
    return true;
}

// Real bug found 2026-09-08 (Faza I point 8's cross-process round-trip
// proof): kernel/mm/paging/paging.c's free_address_space() used to free
// EVERY frame it found mapped in the exiting process's own page tables,
// with no concept of "this one is borrowed, not owned" - a process that
// merely had a SharedMemory frame mapped (via shared_memory_map()/
// shared_memory_map_into(), not the frame's original owner) would free
// it out from under every OTHER process still using it the moment it
// exited. Reproduced concretely: a spawned child wrote a real payload to
// a shared page, signaled done, and called process_exit() - the parent's
// own SUBSEQUENT read of that exact page came back all-zero, because the
// child's own exit had already freed the frame back to the pool, and a
// completely unrelated page-table allocation (for the parent's own first-
// ever mapping in that vaddr region) immediately reused and zeroed it.
// free_address_space() now asks this before freeing any leaf data frame -
// SHM_SLOTS/SHM_MAX_PAGES are both small (4x4), a linear scan here is
// negligible next to the real correctness this closes.
bool shared_memory_owns_frame(void* frame) {
    int i = 0;
    while (i < SHM_SLOTS) {
        if (g_shared_regions[i].used) {
            u32 p = 0;
            while (p < g_shared_regions[i].page_count) {
                if (g_shared_regions[i].frames[p] == frame) {
                    return true;
                }
                p = p + 1;
            }
        }
        i = i + 1;
    }
    return false;
}
