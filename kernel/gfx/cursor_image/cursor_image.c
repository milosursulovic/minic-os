// The mouse cursor's pixel data - a real decoded PNG asset (see png.h),
// embedded straight into kernel.elf by cursor_blob.s's .incbin (same
// convention as the ring3 program blobs, see Makefile). Classic arrow
// shape, hotspot at its top-left corner (mouse_x, mouse_y). Used to be a
// hand-authored pixel table here directly; replaced once the PNG decoder
// existed so any future imported graphic (icons, wallpaper, etc.) can
// follow this exact same asset-blob-plus-png_decode() pattern instead of
// hand-typing pixel tables.

#include "cursor_image.h"
#include "../png/png.h"

#pragma GCC visibility push(hidden)
extern u8 g_cursor_png_start;
extern u8 g_cursor_png_end;
#pragma GCC visibility pop

image g_cursor_image;
static bool g_cursor_image_initialized;

void cursor_image_init(void) {
    if (g_cursor_image_initialized) {
        return;
    }
    u32 size = (u32) ((u64) &g_cursor_png_end - (u64) &g_cursor_png_start);
    if (png_decode(&g_cursor_png_start, size, &g_cursor_image)) {
        g_cursor_image_initialized = true;
    }
}
