// A real free-list allocator (split on alloc, forward *and* backward
// coalesce on free) - milestone 6 built the machinery (allocFrame +
// mapPage) to back memory anywhere the frame allocator can reach, and
// this is that machinery's actual point: instead of a fixed 1MB .bss
// arena, the heap starts small and grows a chunk at a time by mapping
// fresh frames just past its current end, up to gHeapCap. Blocks stay
// in address order, so "the next block" is always `offset +
// sizeof(header) + size` - no separate `next` pointer needed, and
// forward-coalescing two adjacent free blocks is just folding one
// header's size into its neighbor's.

import "frames.mc";
import "paging.mc";

struct BlockHeader {
    u64 size;    // usable bytes *after* this header, not counting the header itself
    bool free;
}

// A dedicated virtual region, well clear of both boot.s's static 1GB
// identity map (0..0x40000000) and the `map` shell command's demo page
// at 0x40000000 - nothing else claims this address.
u64 gHeapBase = 0x50000000;
u64 gHeapSize;       // bytes currently mapped/usable, grows over time
u64 gHeapCap;         // hard cap on growth, so a runaway allocator can't eat every frame
bool gHeapInited;
void* gLastAlloc;

BlockHeader* blockAt(u64 offset) {
    return (BlockHeader*) (gHeapBase + offset);
}

// Grows the heap by at least `minExtra` bytes (rounded up to whole
// pages, at least one 64KB chunk at a time so typical growth isn't one
// page at a call), mapping fresh frames just past the current end.
// Returns false (leaving gHeapSize unchanged) if a frame or a mapping
// fails, or the cap's already been hit - kalloc() treats that as
// out-of-memory, same as a full arena used to mean.
bool heapGrow(u64 minExtra) {
    u64 chunk = minExtra;
    if (chunk < 65536) {
        chunk = 65536;
    }
    if (chunk % 4096 != 0) {
        chunk = chunk + (4096 - (chunk % 4096));
    }
    if (gHeapSize + chunk > gHeapCap) {
        chunk = gHeapCap - gHeapSize;
    }

    u64 mapped = 0;
    while (mapped < chunk) {
        void* frame = allocFrame();
        if (frame == null) {
            return false;
        }
        u64 vaddr = gHeapBase + gHeapSize + mapped;
        if (!mapPage(vaddr, (u64) frame, 0x02)) {
            freeFrame(frame);
            return false;
        }
        mapped = mapped + 4096;
    }
    gHeapSize = gHeapSize + chunk;
    return true;
}

// First call ever bootstraps a 64KB mapped region; every later call
// (e.g. from the `reset` shell command) just collapses the free list
// back into one block over whatever's *already* mapped - re-growing
// here would re-map the same virtual addresses over fresh frames
// without ever freeing the old ones, leaking a frame per byte remapped.
void heapInit() {
    if (gHeapSize == 0) {
        gHeapCap = 16777216;   // 16MB - plenty for a hobby kernel, bounds a runaway allocator
        heapGrow(65536);
    }
    u64 headerSize = sizeof(BlockHeader);
    BlockHeader* first = blockAt(0);
    first->size = gHeapSize - headerSize;
    first->free = true;
    gHeapInited = true;
}

void* kalloc(u64 size) {
    if (!gHeapInited) {
        heapInit();
    }
    if (size % 16 != 0) {
        size = size + (16 - (size % 16));
    }
    u64 headerSize = sizeof(BlockHeader);

    while (true) {
        u64 offset = 0;
        while (offset < gHeapSize) {
            BlockHeader* block = blockAt(offset);
            if (block->free && block->size >= size) {
                // Split off the remainder as a new free block, but only if
                // there's enough room left for another header plus something
                // worth having - otherwise just hand over the whole block.
                if (block->size >= size + headerSize + 16) {
                    u64 remainderOffset = offset + headerSize + size;
                    BlockHeader* remainder = blockAt(remainderOffset);
                    remainder->size = block->size - size - headerSize;
                    remainder->free = true;
                    block->size = size;
                }
                block->free = false;
                u8* blockBytes = (u8*) block;
                return (void*) (blockBytes + headerSize);
            }
            offset = offset + headerSize + block->size;
        }

        // Nothing free was big enough - grow and add the new space as one
        // more free block at the old end, then retry the scan from there.
        u64 oldSize = gHeapSize;
        if (!heapGrow(size + headerSize)) {
            return null;
        }
        BlockHeader* grown = blockAt(oldSize);
        grown->size = (gHeapSize - oldSize) - headerSize;
        grown->free = true;
    }
    return null;   // unreachable - the loop above only ever exits via return
}

void kfree(void* ptr) {
    if (ptr == null) {
        return;
    }
    u64 headerSize = sizeof(BlockHeader);
    u64 ptrAddr = (u64) ptr;
    // A bogus pointer (e.g. a stale/mistyped address from `free <addr>`)
    // would otherwise underflow this subtraction to a huge offset and
    // either corrupt unrelated memory or fault - found this the hard way
    // testing `free <addr>` with an address from a previous build. Ignore
    // it instead of trusting it.
    if (ptrAddr < gHeapBase + headerSize || ptrAddr >= gHeapBase + gHeapSize) {
        return;
    }
    u64 offset = ptrAddr - gHeapBase - headerSize;

    BlockHeader* block = blockAt(offset);
    block->free = true;

    // Forward-coalesce: fold in every immediately-following block while
    // it's also free, since blocks are laid out contiguously in address
    // order - no pointer-chasing needed to find "the next one".
    u64 nextOffset = offset + headerSize + block->size;
    while (nextOffset < gHeapSize) {
        BlockHeader* nextBlock = blockAt(nextOffset);
        if (!nextBlock->free) {
            break;
        }
        block->size = block->size + headerSize + nextBlock->size;
        nextOffset = offset + headerSize + block->size;
    }

    // Backward-coalesce: blocks have no back-pointer, so finding the one
    // immediately *before* this one means rescanning from the arena
    // start - O(n) per free, fine for a hobby heap, not something a real
    // allocator would want.
    u64 scanOffset = 0;
    u64 prevOffset = offset;
    bool foundPrev = false;
    while (scanOffset < offset) {
        BlockHeader* scanBlock = blockAt(scanOffset);
        if (scanOffset + headerSize + scanBlock->size == offset) {
            prevOffset = scanOffset;
            foundPrev = true;
            break;
        }
        scanOffset = scanOffset + headerSize + scanBlock->size;
    }
    if (foundPrev) {
        BlockHeader* prevBlock = blockAt(prevOffset);
        if (prevBlock->free) {
            prevBlock->size = prevBlock->size + headerSize + block->size;
        }
    }
}

u64 heapFreeBytes() {
    if (!gHeapInited) {
        heapInit();
    }
    u64 headerSize = sizeof(BlockHeader);
    u64 total = 0;
    u64 offset = 0;
    while (offset < gHeapSize) {
        BlockHeader* block = blockAt(offset);
        if (block->free) {
            total = total + block->size;
        }
        offset = offset + headerSize + block->size;
    }
    return total;
}
