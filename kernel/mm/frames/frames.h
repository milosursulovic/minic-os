#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Multiboot info pointer, stashed from EBX by boot.s (`_start` takes no params).
extern u32 g_multiboot_info_ptr;

extern u32 g_total_frames;
extern u32 g_free_frame_count;
extern void* g_last_frame;

void frames_init(void);
void* alloc_frame(void);
void free_frame(void* addr);

#pragma GCC visibility pop
