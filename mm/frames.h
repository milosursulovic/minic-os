#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

// Referenced by boot.s (`.extern g_multiboot_info_ptr`) - stashes the
// multiboot info pointer GRUB/QEMU hand off in EBX, since `_start` takes
// no parameters.
extern u32 g_multiboot_info_ptr;

extern u32 g_total_frames;
extern u32 g_free_frame_count;
extern void* g_last_frame;

void frames_init(void);
void* alloc_frame(void);
void free_frame(void* addr);

#pragma GCC visibility pop
