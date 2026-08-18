// A real free-list allocator (split on alloc, forward *and* backward
// coalesce on free) - milestone 6 built the machinery (alloc_frame +
// map_page) to back memory anywhere the frame allocator can reach, and
// this is that machinery's actual point: instead of a fixed 1MB .bss
// arena, the heap starts small and grows a chunk at a time by mapping
// fresh frames just past its current end, up to g_heap_cap. Blocks stay
// in address order, so "the next block" is always `offset +
// sizeof(header) + size` - no separate `next` pointer needed, and
// forward-coalescing two adjacent free blocks is just folding one
// header's size into its neighbor's.

import "frames.mc";
import "paging.mc";

struct block_header {
    u64 size;    // usable bytes *after* this header, not counting the header itself
    bool free;
}

// A dedicated virtual region, well clear of both boot.s's static 1GB
// identity map (0..0x40000000) and the `map` shell command's demo page
// at 0x40000000 - nothing else claims this address.
u64 g_heap_base = 0x50000000;
u64 g_heap_size;       // bytes currently mapped/usable, grows over time
u64 g_heap_cap;         // hard cap on growth, so a runaway allocator can't eat every frame
bool g_heap_inited;
void* g_last_alloc;

block_header* block_at(u64 offset) {
    return (block_header*) (g_heap_base + offset);
}

// Grows the heap by at least `min_extra` bytes (rounded up to whole
// pages, at least one 64KB chunk at a time so typical growth isn't one
// page at a call), mapping fresh frames just past the current end.
// Returns false (leaving g_heap_size unchanged) if a frame or a mapping
// fails, or the cap's already been hit - kalloc() treats that as
// out-of-memory, same as a full arena used to mean.
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
        if (frame == null) {
            return false;
        }
        u64 vaddr = g_heap_base + g_heap_size + mapped;
        // Milestone 28: PAGE_NX - the heap is data, never code; nothing
        // legitimate ever executes from it.
        if (!map_page(vaddr, (u64) frame, 0x02 | page_nx)) {
            free_frame(frame);
            return false;
        }
        mapped = mapped + 4096;
    }
    g_heap_size = g_heap_size + chunk;
    return true;
}

// First call ever bootstraps a 64KB mapped region; every later call
// (e.g. from the `reset` shell command) just collapses the free list
// back into one block over whatever's *already* mapped - re-growing
// here would re-map the same virtual addresses over fresh frames
// without ever freeing the old ones, leaking a frame per byte remapped.
void heap_init() {
    if (g_heap_size == 0) {
        g_heap_cap = 16777216;   // 16MB - plenty for a hobby kernel, bounds a runaway allocator
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

    while (true) {
        u64 offset = 0;
        while (offset < g_heap_size) {
            block_header* block = block_at(offset);
            if (block->free && block->size >= size) {
                // Split off the remainder as a new free block, but only if
                // there's enough room left for another header plus something
                // worth having - otherwise just hand over the whole block.
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

        // Nothing free was big enough - grow and add the new space as one
        // more free block at the old end, then retry the scan from there.
        u64 old_size = g_heap_size;
        if (!heap_grow(size + header_size)) {
            return null;
        }
        block_header* grown = block_at(old_size);
        grown->size = (g_heap_size - old_size) - header_size;
        grown->free = true;
    }
    return null;   // unreachable - the loop above only ever exits via return
}

void kfree(void* ptr) {
    if (ptr == null) {
        return;
    }
    u64 header_size = sizeof(block_header);
    u64 ptr_addr = (u64) ptr;
    // A bogus pointer (e.g. a stale/mistyped address from `free <addr>`)
    // would otherwise underflow this subtraction to a huge offset and
    // either corrupt unrelated memory or fault - found this the hard way
    // testing `free <addr>` with an address from a previous build. Ignore
    // it instead of trusting it.
    if (ptr_addr < g_heap_base + header_size || ptr_addr >= g_heap_base + g_heap_size) {
        return;
    }
    u64 offset = ptr_addr - g_heap_base - header_size;

    block_header* block = block_at(offset);
    block->free = true;

    // Forward-coalesce: fold in every immediately-following block while
    // it's also free, since blocks are laid out contiguously in address
    // order - no pointer-chasing needed to find "the next one".
    u64 next_offset = offset + header_size + block->size;
    while (next_offset < g_heap_size) {
        block_header* next_block = block_at(next_offset);
        if (!next_block->free) {
            break;
        }
        block->size = block->size + header_size + next_block->size;
        next_offset = offset + header_size + block->size;
    }

    // Backward-coalesce: blocks have no back-pointer, so finding the one
    // immediately *before* this one means rescanning from the arena
    // start - O(n) per free, fine for a hobby heap, not something a real
    // allocator would want.
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

u64 heap_free_bytes() {
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
