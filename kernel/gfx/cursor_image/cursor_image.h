#pragma once

#include "../image/image.h"

#pragma GCC visibility push(hidden)

// A static initializer can't set g_cursor_image.pixels to g_cursor_pixels
// directly - that's one global's address stored as another global's
// static data, which needs a real ELF64 relocation record (R_X86_64_64)
// that this kernel's ELF32 build container can't represent (same
// constraint CLAUDE.md documents for hand-written asm's `.quad <label>`,
// just hit here in C instead: -fPIC makes *code* use RIP-relative `lea`
// for an address-of, but a data-to-data static initializer isn't code -
// there's no instruction to make PC-relative, only a load-time-patched
// pointer, so the relocation is unavoidable regardless of -fPIC). Fixed
// by assigning the pointer at runtime instead (a real `mov`/`lea`
// instruction, which -fPIC handles fine) - call this once before the
// first draw_cursor(); idempotent, safe to call more than once.
void cursor_image_init(void);

extern image g_cursor_image;

#pragma GCC visibility pop
