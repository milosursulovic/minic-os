#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

#define FONT_GLYPH_WIDTH 5
#define FONT_GLYPH_HEIGHT 7

// Each row is bits 4..0 = pixels left..right, bit set = foreground pixel.
// Returns false for any character outside the hand-authored set (A-Z, a-z,
// 0-9, space, and . , ! ? : - > /) - callers render those as blank space,
// not a crash or a placeholder glyph. Lowercase letters have no true
// descender (drawn within the same fixed 5x7 box uppercase uses - see
// font.c's own comment).
bool font_get_glyph(char c, u8 rows[FONT_GLYPH_HEIGHT]);

#pragma GCC visibility pop
