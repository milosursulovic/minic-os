#pragma once

#include "../image/image.h"

#pragma GCC visibility push(hidden)

// Decodes the embedded cursor.png (kernel/gfx/png/cursor_blob.s) into
// g_cursor_image via the real PNG decoder (kernel/gfx/png/png.c) - call
// this once before the first draw_cursor(); idempotent, safe to call
// more than once (a no-op once already decoded).
void cursor_image_init(void);

extern image g_cursor_image;

#pragma GCC visibility pop
