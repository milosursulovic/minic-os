#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Multiboot info pointer, stashed from EBX by boot.s (`_start` takes no params).
extern u32 g_multiboot_info_ptr;
// Real multiboot magic, stashed from EAX by boot.s - 0x2BADB002 means
// g_multiboot_info_ptr is the flat multiboot1 `multiboot_info` struct
// below; 0x36D76289 (MULTIBOOT2_MAGIC) means it's a completely
// different multiboot2 tag-list format instead (see multiboot2_*
// structs below) - the two are NOT interchangeable, reading one as the
// other silently misinterprets memory.
extern u32 g_multiboot_magic;
static const u32 MULTIBOOT2_BOOTLOADER_MAGIC = 0x36D76289;

// Full real multiboot1 info structure (was frames.c-private and only
// defined through mmap_addr) - extended so kernel/drivers/vbe/vbe.c can
// read the real framebuffer fields boot.s's MB_FLAGS bit2 now requests.
// Every field before framebuffer_addr is currently unused by this
// kernel but position-critical - packed, so getting the shape right
// gets every later offset right without guessing (framebuffer_addr
// really does land at byte offset 88 per the real multiboot1 spec).
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
    u32 drives_length;
    u32 drives_addr;
    u32 config_table;
    u32 boot_loader_name;
    u32 apm_table;
    u32 vbe_control_info;
    u32 vbe_mode_info;
    u16 vbe_mode;
    u16 vbe_interface_seg;
    u16 vbe_interface_off;
    u16 vbe_interface_len;
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8 framebuffer_bpp;
    u8 framebuffer_type;
    u8 color_info[6];  // palette/RGB field-position info - unused, present only to keep the struct's real total size honest
} multiboot_info;

// Flags bit 12: framebuffer_* fields below mmap are valid (set by
// GRUB/QEMU only when boot.s's MB_FLAGS bit2 request was honored).
static const u32 MULTIBOOT_INFO_FLAG_FRAMEBUFFER = 0x1000;
// framebuffer_type == 1: RGB direct-color (not indexed/EGA-text) - the
// only kind this kernel's fb_put_pixel()/compositor ever assumed.
static const u8 MULTIBOOT_FRAMEBUFFER_TYPE_RGB = 1;

// ---- Multiboot2 boot-info structures (completely different shape
// from multiboot1's flat struct above - a `total_size`/`reserved`
// header followed by a real tag list, each tag 8-byte aligned, real
// spec confirmed via GNU GRUB's own multiboot2.h) ----
typedef struct __attribute__((packed)) {
    u32 total_size;
    u32 reserved;
    // tags immediately follow, first one right here
} multiboot2_info;

// Common prefix every multiboot2 boot-info tag starts with - real
// per-tag size (not padded) tells you where this tag's own data ends;
// the NEXT tag starts at that offset rounded up to 8 bytes.
typedef struct __attribute__((packed)) {
    u32 type;
    u32 size;
} multiboot2_tag;

#define MULTIBOOT2_TAG_TYPE_END 0
#define MULTIBOOT2_TAG_TYPE_MMAP 6
#define MULTIBOOT2_TAG_TYPE_FRAMEBUFFER 8

typedef struct __attribute__((packed)) {
    u64 addr;
    u64 len;
    u32 type;  // 1 = available RAM - same meaning as multiboot1's mmap_entry.type
    u32 zero;
} multiboot2_mmap_entry;

typedef struct __attribute__((packed)) {
    u32 type;  // MULTIBOOT2_TAG_TYPE_MMAP
    u32 size;
    u32 entry_size;    // real per-entry size (spec-defined, usually 24 - never assumed, always used to step the array)
    u32 entry_version;
    // multiboot2_mmap_entry entries[] immediately follow, entry_size bytes apart
} multiboot2_tag_mmap;

typedef struct __attribute__((packed)) {
    u32 type;  // MULTIBOOT2_TAG_TYPE_FRAMEBUFFER
    u32 size;
    u64 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u8 framebuffer_bpp;
    u8 framebuffer_type;
    u16 reserved;
    // color-info union (palette or RGB field-position bytes) follows -
    // unused, this kernel's own 32bpp-RGB-only assumption never needs it.
} multiboot2_tag_framebuffer;

// Real per-spec tag-list walk: rounds `size` up to the next 8-byte
// boundary to find the next tag, stops at the type-0 end tag. Shared
// by frames.c (memory map) and vbe.c (framebuffer) so both real
// multiboot2 consumers step the list identically.
static inline multiboot2_tag* multiboot2_next_tag(multiboot2_tag* tag) {
    u64 addr = (u64) tag;
    u64 advance = (tag->size + 7u) & ~7u;
    return (multiboot2_tag*) (addr + advance);
}

extern u32 g_total_frames;
extern u32 g_free_frame_count;
extern void* g_last_frame;

void frames_init(void);
void* alloc_frame(void);
void free_frame(void* addr);

#pragma GCC visibility pop
