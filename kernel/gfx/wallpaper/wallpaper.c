// The default desktop wallpaper's pixel data - a real decoded PNG asset
// (see png.h), embedded straight into kernel.elf by wallpaper_blob.s's
// .incbin (same convention as kernel/gfx/cursor_image/cursor_image.c's
// cursor.png). 800x600 to match desktop_shell.c's own borderless
// wallpaper window exactly - no scaling support exists (see image.h's
// draw_image), so the asset itself is generated at the real target size.

#include "wallpaper.h"
#include "../png/png.h"

#pragma GCC visibility push(hidden)
extern u8 g_wallpaper_png_start;
extern u8 g_wallpaper_png_end;
#pragma GCC visibility pop

image g_wallpaper_image;
static bool g_wallpaper_image_initialized;

void wallpaper_image_init(void) {
    if (g_wallpaper_image_initialized) {
        return;
    }
    u32 size = (u32) ((u64) &g_wallpaper_png_end - (u64) &g_wallpaper_png_start);
    if (png_decode(&g_wallpaper_png_start, size, &g_wallpaper_image)) {
        g_wallpaper_image_initialized = true;
    }
}
