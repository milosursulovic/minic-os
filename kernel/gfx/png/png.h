#pragma once

#include "../image/image.h"

#pragma GCC visibility push(hidden)

// Hand-written PNG decoder. Deliberately scoped to what any real,
// hand-authored icon/cursor PNG needs and no more: 8-bit-per-channel,
// non-interlaced, color type 2 (truecolor RGB) or 6 (truecolor+alpha).
// Anything else - palette, grayscale, 16-bit depth, Adam7 interlacing -
// is a real, deliberate rejection (returns false), not a bug; PIL's own
// Image.convert("RGB"/"RGBA") always produces a PNG inside this subset.
//
// Validates every chunk's CRC32 and the zlib stream's Adler32 - a
// corrupted PNG fails cleanly (false) instead of producing garbage
// pixels.
//
// On success, out->pixels is a freshly kalloc'd width*height array,
// never freed (same permanent-global lifetime as any image this decodes
// into, e.g. gfx/cursor_image.c's g_cursor_image) - in the same
// 0x00RRGGBB / IMAGE_TRANSPARENT convention as every other image in this
// codebase (gfx/image.h). RGBA source pixels with alpha==0 become
// IMAGE_TRANSPARENT, any other alpha becomes fully opaque - binary
// transparency only, matching this codebase's existing all-or-nothing
// compositing (no partial alpha blending exists anywhere else either).
bool png_decode(const u8* data, u32 size, image* out);

#pragma GCC visibility pop
