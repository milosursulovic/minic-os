#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Multiboot info pointer, stashed from EBX by boot.s (`_start` takes no params).
extern u32 g_multiboot_info_ptr;

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

extern u32 g_total_frames;
extern u32 g_free_frame_count;
extern void* g_last_frame;

void frames_init(void);
void* alloc_frame(void);
void free_frame(void* addr);

#pragma GCC visibility pop
