// Physical frame allocator: 1 bit per 4KB frame over the identity-mapped 1GB,
// built from the multiboot memory map. Tracks physical frames, not the
// virtual bytes heap.c hands out.

#include "frames.h"

typedef struct __attribute__((packed)) {
    u32 flags;
    u32 mem_lower;
    u32 mem_upper;
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    u32 syms0;
    u32 syms1;
    u32 syms2;
    u32 syms3;
    u32 mmap_length;
    u32 mmap_addr;
} multiboot_info;

// packed: `addr` sits at an unaligned 4-byte offset per the real multiboot spec.
typedef struct __attribute__((packed)) {
    u32 size;
    u64 addr;
    u64 len;
    u32 type;  // 1 = available RAM
} mmap_entry;

u32 g_multiboot_info_ptr;

static u8 g_frame_bitmap[32768];
u32 g_total_frames;
u32 g_free_frame_count;
void* g_last_frame;

static void frame_set(u32 frame) {
    u32 byte_index = frame / 8;
    u8 mask = (u8) (1 << (frame % 8));
    g_frame_bitmap[byte_index] = g_frame_bitmap[byte_index] | mask;
}

static bool frame_test(u32 frame) {
    u32 byte_index = frame / 8;
    u8 mask = (u8) (1 << (frame % 8));
    return (g_frame_bitmap[byte_index] & mask) != 0;
}

static void frame_clear(u32 frame) {
    u32 byte_index = frame / 8;
    u8 mask = (u8) (1 << (frame % 8));
    g_frame_bitmap[byte_index] = g_frame_bitmap[byte_index] & (u8) (~mask);
}

// Everything starts "used"; the memory map clears what's actually free.
// First 4MB is reserved unconditionally rather than computed precisely.
void frames_init(void) {
    u32 i = 0;
    while (i < 32768) {
        g_frame_bitmap[i] = 255;
        i = i + 1;
    }
    g_total_frames = 262144;  // 1GB / 4KB
    g_free_frame_count = 0;

    multiboot_info* info = (multiboot_info*) ((u64) g_multiboot_info_ptr);
    u64 mmap_addr = (u64) info->mmap_addr;
    u64 mmap_end = mmap_addr + (u64) info->mmap_length;
    u64 entry_addr = mmap_addr;
    while (entry_addr < mmap_end) {
        mmap_entry* entry = (mmap_entry*) entry_addr;
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
            u64 frame_end = end / 4096;
            while (frame < frame_end) {
                if (frame_test((u32) frame)) {
                    frame_clear((u32) frame);
                    g_free_frame_count = g_free_frame_count + 1;
                }
                frame = frame + 1;
            }
        }
        entry_addr = entry_addr + (u64) entry->size + 4;
    }
}

void* alloc_frame(void) {
    u32 i = 0;
    while (i < g_total_frames) {
        if (!frame_test(i)) {
            frame_set(i);
            g_free_frame_count = g_free_frame_count - 1;
            u64 addr = (u64) i * 4096;
            return (void*) addr;
        }
        i = i + 1;
    }
    return NULL;
}

void free_frame(void* addr) {
    u64 a = (u64) addr;
    u32 frame = (u32) (a / 4096);
    if (frame_test(frame)) {
        frame_clear(frame);
        g_free_frame_count = g_free_frame_count + 1;
    }
}
