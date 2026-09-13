#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

extern char g_scancode_table[128];
// Shifted (uppercase letters + real shifted symbols) counterpart of
// g_scancode_table - Faza II point 17: real Shift tracking. Only covers
// the same key set g_scancode_table already does (no full 101-key layout
// - matches this codebase's existing "letters/digits/space/enter/./ only"
// scope, just doubled for the shifted case).
extern char g_scancode_table_shifted[128];

extern char g_line_buffer[128];
extern int g_line_len;
extern bool g_line_ready;
// Position within the line where typing/Backspace/Delete act - always
// 0 <= g_line_cursor <= g_line_len. Equal to g_line_len (cursor at the
// end) is the common case and behaves exactly like plain append-only
// typing always did; kernel/isr/isr.c's Left/Right arrow handling is the
// only thing that ever moves it away from the end.
extern int g_line_cursor;

void init_scancode_table(void);

#pragma GCC visibility pop
