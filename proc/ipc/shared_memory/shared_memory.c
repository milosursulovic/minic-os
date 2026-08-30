#include "shared_memory.h"
#include "../../../kernel/mm/frames/frames.h"
#include "../../../kernel/mm/paging/paging.h"

shared_region g_shared_regions[SHM_SLOTS];

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
    int slot = find_free_slot();
    if (slot < 0) {
        return -1;
    }

    u32 p = 0;
    while (p < page_count) {
        void* frame = alloc_frame();
        if (frame == NULL) {
            return -1;
        }
        g_shared_regions[slot].frames[p] = frame;
        p = p + 1;
    }
    g_shared_regions[slot].page_count = page_count;
    g_shared_regions[slot].used = true;
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
