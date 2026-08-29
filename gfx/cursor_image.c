// The mouse cursor's pixel data, as a real image asset (see image.h) -
// moved out of gfx/window.c's compositor code, which used to hand-branch
// this exact bitmap directly. Classic arrow shape, hotspot at its
// top-left corner (mouse_x, mouse_y) - same convention as every desktop
// pointer: the head grows as a triangle, then is cut off flat partway
// down and finishes with a narrower heel/tail below the cut, which is
// what actually reads as an arrow rather than a plain wedge.
//
// T = transparent (background shows through), B = black outline,
// W = white fill - kept as short local aliases purely so the 11-wide,
// 16-tall shape below still reads as a picture, the same reason
// gfx/font.h's glyph table uses 0b-binary literals instead of decimal.

#include "cursor_image.h"

#define T IMAGE_TRANSPARENT
#define B 0x00000000u
#define W 0x00FFFFFFu

static const u32 g_cursor_pixels[16 * 11] = {
    B,T,T,T,T,T,T,T,T,T,T,
    B,B,T,T,T,T,T,T,T,T,T,
    B,W,B,T,T,T,T,T,T,T,T,
    B,W,W,B,T,T,T,T,T,T,T,
    B,W,W,W,B,T,T,T,T,T,T,
    B,W,W,W,W,B,T,T,T,T,T,
    B,W,W,W,W,W,B,T,T,T,T,
    B,W,W,W,W,W,W,B,T,T,T,
    B,W,W,W,W,W,W,W,B,T,T,
    B,W,W,W,W,W,W,W,W,B,T,
    B,W,W,W,W,W,B,B,B,B,B,  // flat cut across the head's base
    B,W,W,B,B,T,T,T,T,T,T,
    B,W,B,W,B,T,T,T,T,T,T,
    B,B,T,W,B,T,T,T,T,T,T,
    B,T,T,T,W,B,T,T,T,T,T,
    T,T,T,T,T,B,T,T,T,T,T,
};

#undef T
#undef B
#undef W

image g_cursor_image;
static bool g_cursor_image_initialized;

void cursor_image_init(void) {
    if (g_cursor_image_initialized) {
        return;
    }
    g_cursor_image.width = 11;
    g_cursor_image.height = 16;
    g_cursor_image.pixels = g_cursor_pixels;
    g_cursor_image_initialized = true;
}
