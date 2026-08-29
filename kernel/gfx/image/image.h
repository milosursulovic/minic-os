#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// A minimal, hand-authored image asset - a flat row-major pixel array,
// same "no external library, no real file-format decoding" spirit as
// gfx/font.h's hand-authored glyph bitmaps. No alpha channel field - every
// real color in this codebase is already 0x00RRGGBB (top byte always
// zero), so IMAGE_TRANSPARENT reuses that byte as a sentinel no real
// color can ever collide with, instead of widening every pixel.
typedef struct {
    u32 width;
    u32 height;
    const u32* pixels;  // width*height, row-major; IMAGE_TRANSPARENT pixels are skipped
} image;

#define IMAGE_TRANSPARENT 0xFFFFFFFFu

// Draws img with its top-left corner at (x, y) into the compositor's
// backbuffer (gfx/window.c's bb_put_pixel - already bounds-checked, so an
// image partly or fully off-screen is safe). No scaling/rotation - a
// straight, unscaled blit, the same complexity level as every other
// drawing primitive here (window_fill_content_rect, window_draw_text).
void draw_image(u32 x, u32 y, const image* img);

#pragma GCC visibility pop
