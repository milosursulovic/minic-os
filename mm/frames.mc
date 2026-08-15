// Physical memory: parsing the multiboot memory map (handed to the
// kernel via EBX at entry, preserved by boot.s into gMultibootInfoPtr
// since `_start` takes no parameters) and a frame bitmap allocator built
// from it - 1 bit per 4KB frame, covering the whole milestone-1
// identity-mapped 1GB (32768 bytes * 8 bits * 4KB = 1GB). Distinct from
// heap.mc: that hands out *virtual* bytes within a fixed 1MB arena for
// kernel data structures; this tracks *physical* frames, the raw
// material paging.mc's mapPage() uses to back new virtual mappings.

packed struct MultibootInfo {
    u32 flags;
    u32 memLower;
    u32 memUpper;
    u32 bootDevice;
    u32 cmdline;
    u32 modsCount;
    u32 modsAddr;
    u32 syms0;
    u32 syms1;
    u32 syms2;
    u32 syms3;
    u32 mmapLength;
    u32 mmapAddr;
}

// `size` is the byte count of the rest of THIS entry, not counting itself
// - entries aren't necessarily a fixed stride, so `addr` genuinely does
// sit at an unaligned 4-byte offset by the real spec. Without `packed`,
// MiniC would insert 4 bytes of padding before `addr` to 8-byte-align it
// and silently read the wrong bytes - exactly the case `packed` exists for.
packed struct MmapEntry {
    u32 size;
    u64 addr;
    u64 len;
    u32 type;   // 1 = available RAM
}

u32 gMultibootInfoPtr;

u8 gFrameBitmap[32768];
u32 gTotalFrames;
u32 gFreeFrameCount;
void* gLastFrame;

void frameSet(u32 frame) {
    u32 byteIndex = frame / 8;
    u8 mask = (u8) (1 << (frame % 8));
    gFrameBitmap[byteIndex] = gFrameBitmap[byteIndex] | mask;
}

bool frameTest(u32 frame) {
    u32 byteIndex = frame / 8;
    u8 mask = (u8) (1 << (frame % 8));
    return (gFrameBitmap[byteIndex] & mask) != 0;
}

void frameClear(u32 frame) {
    u32 byteIndex = frame / 8;
    u8 mask = (u8) (1 << (frame % 8));
    gFrameBitmap[byteIndex] = gFrameBitmap[byteIndex] & (~mask);
}

// Everything starts "used"; the multiboot memory map (type 1 = available
// RAM) clears the frames that are actually free to hand out. The first
// 4MB is reserved unconditionally regardless of what the map says -
// simpler than computing exactly where the kernel image/heap arena/this
// very bitmap end, and there's plenty of room to spare.
void framesInit() {
    u32 i = 0;
    while (i < 32768) {
        gFrameBitmap[i] = 255;
        i = i + 1;
    }
    gTotalFrames = 262144;   // 1GB / 4KB
    gFreeFrameCount = 0;

    MultibootInfo* info = (MultibootInfo*) ((u64) gMultibootInfoPtr);
    u64 mmapAddr = (u64) info->mmapAddr;
    u64 mmapEnd = mmapAddr + (u64) info->mmapLength;
    u64 entryAddr = mmapAddr;
    while (entryAddr < mmapEnd) {
        MmapEntry* entry = (MmapEntry*) entryAddr;
        if (entry->type == 1) {
            u64 start = entry->addr;
            u64 end = entry->addr + entry->len;
            if (start < 4194304) {
                start = 4194304;
            }
            if (end > 1073741824) {
                end = 1073741824;
            }
            u64 frame = start / 4096;
            u64 frameEnd = end / 4096;
            while (frame < frameEnd) {
                if (frameTest((u32) frame)) {
                    frameClear((u32) frame);
                    gFreeFrameCount = gFreeFrameCount + 1;
                }
                frame = frame + 1;
            }
        }
        entryAddr = entryAddr + (u64) entry->size + 4;
    }
}

void* allocFrame() {
    u32 i = 0;
    while (i < gTotalFrames) {
        if (!frameTest(i)) {
            frameSet(i);
            gFreeFrameCount = gFreeFrameCount - 1;
            u64 addr = (u64) i * 4096;
            return (void*) addr;
        }
        i = i + 1;
    }
    return null;
}

void freeFrame(void* addr) {
    u64 a = (u64) addr;
    u32 frame = (u32) (a / 4096);
    if (frameTest(frame)) {
        frameClear(frame);
        gFreeFrameCount = gFreeFrameCount + 1;
    }
}
