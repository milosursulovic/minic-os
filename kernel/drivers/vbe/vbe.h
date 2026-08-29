#pragma once

#include "../../../types.h"

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
// Draws one hand-authored 5x7 glyph (A-Z, 0-9, space, . , ! ? : - only -
// see gfx/font.h) at its top-left pixel; unsupported characters are a no-op.
void fb_draw_char(u32 x, u32 y, char c, u32 fg, u32 bg);
// Single line only, advances FONT_GLYPH_WIDTH + 1 px per character.
void fb_draw_string(u32 x, u32 y, const char* s, u32 fg, u32 bg);

#pragma GCC visibility pop
