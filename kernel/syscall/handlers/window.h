#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Window/mouse/input - 26-34 (create/move/raise/close/fill_rect/
// draw_text/query/create_borderless, mouse_query), 58
// (register_terminal_window), 69/70 (focus_window/read_key), 94
// (draw_wallpaper - real desktop wallpaper image, kernel/gfx/wallpaper/).
bool syscall_window(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
