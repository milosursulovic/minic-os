// A real free-list allocator (split on alloc, forward *and* backward
// coalesce on free) over a reserved 1MB arena, backed by .bss and
// already covered by the flat identity map from milestone 1. Blocks
// stay in address order, so "the next block" is always `offset +
// sizeof(header) + size` - no separate `next` pointer needed, and
// forward-coalescing two adjacent free blocks is just folding one
// header's size into its neighbor's.

struct BlockHeader {
    u64 size;    // usable bytes *after* this header, not counting the header itself
    bool free;
}

u8 gHeapArena[1048576];
bool gHeapInited;
void* gLastAlloc;

BlockHeader* blockAt(u64 offset) {
    u8* base = gHeapArena;
    return (BlockHeader*) (base + offset);
}

void heapInit() {
    u64 headerSize = sizeof(BlockHeader);
    BlockHeader* first = blockAt(0);
    first->size = 1048576 - headerSize;
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

    u64 offset = 0;
    while (offset < 1048576) {
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
    return null;
}

void kfree(void* ptr) {
    if (ptr == null) {
        return;
    }
    u64 headerSize = sizeof(BlockHeader);
    u8* base = gHeapArena;
    u64 baseAddr = (u64) base;
    u64 ptrAddr = (u64) ptr;
    // A bogus pointer (e.g. a stale/mistyped address from `free <addr>`)
    // would otherwise underflow this subtraction to a huge offset and
    // either corrupt unrelated memory or fault - found this the hard way
    // testing `free <addr>` with an address from a previous build. Ignore
    // it instead of trusting it.
    if (ptrAddr < baseAddr + headerSize || ptrAddr >= baseAddr + 1048576) {
        return;
    }
    u64 offset = ptrAddr - baseAddr - headerSize;

    BlockHeader* block = blockAt(offset);
    block->free = true;

    // Forward-coalesce: fold in every immediately-following block while
    // it's also free, since blocks are laid out contiguously in address
    // order - no pointer-chasing needed to find "the next one".
    u64 nextOffset = offset + headerSize + block->size;
    while (nextOffset < 1048576) {
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
    while (offset < 1048576) {
        BlockHeader* block = blockAt(offset);
        if (block->free) {
            total = total + block->size;
        }
        offset = offset + headerSize + block->size;
    }
    return total;
}
