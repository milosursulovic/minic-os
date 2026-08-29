// Free-list allocator (split on alloc, forward+backward coalesce on free),
// growing on demand by mapping fresh frames. Blocks stay in address order,
// so there's no separate `next` pointer.

#include "heap.h"
#include "../frames/frames.h"
#include "../paging/paging.h"

typedef struct {
    u64 size;  // usable bytes after this header
    bool free;
} block_header;

static const u64 HEAP_BASE = 0x50000000;
u64 g_heap_size;  // bytes currently mapped/usable, grows over time
u64 g_heap_cap;   // hard cap on growth, so a runaway allocator can't eat every frame
static bool g_heap_inited;
void* g_last_alloc;

static block_header* block_at(u64 offset) {
    return (block_header*) (HEAP_BASE + offset);
}

// Grows by at least min_extra bytes (rounded up, min 64KB chunk). Returns
// false on allocation failure or cap hit; kalloc() treats that as OOM.
bool heap_grow(u64 min_extra) {
    u64 chunk = min_extra;
    if (chunk < 65536) {
        chunk = 65536;
    }
    if (chunk % 4096 != 0) {
        chunk = chunk + (4096 - (chunk % 4096));
    }
    if (g_heap_size + chunk > g_heap_cap) {
        chunk = g_heap_cap - g_heap_size;
    }

    u64 mapped = 0;
    while (mapped < chunk) {
        void* frame = alloc_frame();
        if (frame == NULL) {
            return false;
        }
        u64 vaddr = HEAP_BASE + g_heap_size + mapped;
        if (!map_page(vaddr, (u64) frame, 0x02 | PAGE_NX)) {  // heap is data, never executable
            free_frame(frame);
            return false;
        }
        mapped = mapped + 4096;
    }
    g_heap_size = g_heap_size + chunk;
    return true;
}

// Later calls (e.g. `reset`) must not re-grow - that would remap the same
// vaddrs over fresh frames, leaking the old ones. Just collapse to one free block.
void heap_init(void) {
    if (g_heap_size == 0) {
        g_heap_cap = 16777216;  // 16MB cap
        heap_grow(65536);
    }
    u64 header_size = sizeof(block_header);
    block_header* first = block_at(0);
    first->size = g_heap_size - header_size;
    first->free = true;
    g_heap_inited = true;
}

void* kalloc(u64 size) {
    if (!g_heap_inited) {
        heap_init();
    }
    if (size % 16 != 0) {
        size = size + (16 - (size % 16));
    }
    u64 header_size = sizeof(block_header);

    for (;;) {
        u64 offset = 0;
        while (offset < g_heap_size) {
            block_header* block = block_at(offset);
            if (block->free && block->size >= size) {
                // Split only if enough remains for another header + 16 bytes.
                if (block->size >= size + header_size + 16) {
                    u64 remainder_offset = offset + header_size + size;
                    block_header* remainder = block_at(remainder_offset);
                    remainder->size = block->size - size - header_size;
                    remainder->free = true;
                    block->size = size;
                }
                block->free = false;
                u8* block_bytes = (u8*) block;
                return (void*) (block_bytes + header_size);
            }
            offset = offset + header_size + block->size;
        }

        // Nothing free was big enough - grow and retry.
        u64 old_size = g_heap_size;
        if (!heap_grow(size + header_size)) {
            return NULL;
        }
        block_header* grown = block_at(old_size);
        grown->size = (g_heap_size - old_size) - header_size;
        grown->free = true;
    }
}

void kfree(void* ptr) {
    if (ptr == NULL) {
        return;
    }
    u64 header_size = sizeof(block_header);
    u64 ptr_addr = (u64) ptr;
    // Reject out-of-range pointers rather than let the offset subtraction underflow.
    if (ptr_addr < HEAP_BASE + header_size || ptr_addr >= HEAP_BASE + g_heap_size) {
        return;
    }
    u64 offset = ptr_addr - HEAP_BASE - header_size;

    block_header* block = block_at(offset);
    block->free = true;

    // Forward-coalesce with every immediately-following free block.
    u64 next_offset = offset + header_size + block->size;
    while (next_offset < g_heap_size) {
        block_header* next_block = block_at(next_offset);
        if (!next_block->free) {
            break;
        }
        block->size = block->size + header_size + next_block->size;
        next_offset = offset + header_size + block->size;
    }

    // Backward-coalesce: no back-pointer, so find the previous block by rescanning
    // from the start - O(n) per free.
    u64 scan_offset = 0;
    u64 prev_offset = offset;
    bool found_prev = false;
    while (scan_offset < offset) {
        block_header* scan_block = block_at(scan_offset);
        if (scan_offset + header_size + scan_block->size == offset) {
            prev_offset = scan_offset;
            found_prev = true;
            break;
        }
        scan_offset = scan_offset + header_size + scan_block->size;
    }
    if (found_prev) {
        block_header* prev_block = block_at(prev_offset);
        if (prev_block->free) {
            prev_block->size = prev_block->size + header_size + block->size;
        }
    }
}

u64 heap_free_bytes(void) {
    if (!g_heap_inited) {
        heap_init();
    }
    u64 header_size = sizeof(block_header);
    u64 total = 0;
    u64 offset = 0;
    while (offset < g_heap_size) {
        block_header* block = block_at(offset);
        if (block->free) {
            total = total + block->size;
        }
        offset = offset + header_size + block->size;
    }
    return total;
}
