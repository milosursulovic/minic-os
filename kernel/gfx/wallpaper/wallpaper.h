#pragma once

#include "../image/image.h"

#pragma GCC visibility push(hidden)

// Decodes the embedded wallpaper.png (kernel/gfx/png/wallpaper_blob.s)
// into g_wallpaper_image via the real PNG decoder (kernel/gfx/png/png.c)
// - same lazy-init pattern as kernel/gfx/cursor_image/cursor_image.c's
// cursor_image_init(). Call this once before the first
// window_draw_wallpaper(); idempotent, safe to call more than once.
void wallpaper_image_init(void);

extern image g_wallpaper_image;

#pragma GCC visibility pop
