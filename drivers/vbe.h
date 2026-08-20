#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

extern u64 g_fb_vaddr;
extern u32 g_fb_width;
extern u32 g_fb_height;
extern u32 g_fb_pitch;
extern bool g_fb_enabled;

bool vbe_init(u32 width, u32 height);
u16 vbe_read_reg(u16 index);
u32 vbe_lfb_phys(void);
void fb_put_pixel(u32 x, u32 y, u32 color);
u32 fb_get_pixel(u32 x, u32 y);
void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color);

#pragma GCC visibility pop
