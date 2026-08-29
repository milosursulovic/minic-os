#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

#define WINDOW_SLOTS 8
#define TITLEBAR_HEIGHT 20
#define WINDOW_BACKGROUND_COLOR 0x00202020u

// Caps a window's body (not counting the titlebar, unless borderless) -
// and its content buffer, which is sized for the max rather than tracked
// per-window. Both are the full screen size so a borderless window can
// span it (the desktop shell's 800x600 wallpaper) - this check applies
// unconditionally, even to a flat-body_color window that never draws
// content, so the wallpaper needs real headroom here, not just enough
// for the 800x20 taskbar. Real static memory cost (WINDOW_SLOTS * WIDTH *
// HEIGHT * 4 bytes = 8 * 800 * 600 * 4 ~= 15.4MB), accepted deliberately.
#define WINDOW_CONTENT_MAX_WIDTH 800
#define WINDOW_CONTENT_MAX_HEIGHT 600

typedef struct {
    bool used;
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    u32 body_color;
    u32 title_color;
    bool has_content;
    // No titlebar drawn/reserved - the whole height is body. Only
    // window_create_borderless() sets this; window_create() never does.
    bool borderless;
} window;

extern window g_windows[WINDOW_SLOTS];
extern int g_window_zorder[WINDOW_SLOTS];
extern int g_window_zorder_count;
// [id][y * WINDOW_CONTENT_MAX_WIDTH + x], local to the window's body (y=0 is
// the row right below the titlebar) - only meaningful once has_content.
extern u32 g_window_content[WINDOW_SLOTS][WINDOW_CONTENT_MAX_WIDTH * WINDOW_CONTENT_MAX_HEIGHT];

// Returns the new window's id (its g_windows[] slot), or -1 if out of slots,
// the body exceeds the content-buffer cap, or the bounds don't fit on
// screen. New windows land on top.
int window_create(i32 x, i32 y, u32 width, u32 height, u32 body_color, u32 title_color);
// Same slot/z-order logic as window_create(), but no titlebar is drawn or
// reserved - height only has to fit WINDOW_CONTENT_MAX_HEIGHT, not clear
// TITLEBAR_HEIGHT first. Returns -1 on the same failure conditions.
int window_create_borderless(i32 x, i32 y, u32 width, u32 height, u32 body_color);
bool window_move(int id, i32 x, i32 y);
bool window_close(int id);
// Moves id to the top of the z-order - the compositor draws it last.
bool window_raise(int id);
// Draws into id's content buffer, body-local coordinates, clamped to its
// body size. Once called, the compositor draws this buffer instead of
// body_color - a window is either a flat placeholder or fully app-drawn.
bool window_fill_content_rect(int id, u32 x, u32 y, u32 w, u32 h, u32 color);
// Draws text into id's content buffer, same body-local clipping as
// window_fill_content_rect. Single line only; unsupported characters (see
// gfx/font.h) leave their cell untouched.
bool window_draw_text(int id, u32 x, u32 y, const char* text, u32 fg, u32 bg);
// Body-local origin (screen coords of body's top-left, past titlebar) +
// body dims - matches window_fill_content_rect/window_draw_text's own
// coordinate origin, so ring3 hit-testing needs no TITLEBAR_HEIGHT const.
bool window_query(int id, i32* body_x, i32* body_y, u32* body_width, u32* body_height);
// Fills the background, then draws every window bottom-to-top.
void compositor_redraw(void);
// Writes one pixel into the compositor's off-screen backbuffer (bounds-
// checked, out-of-range is a silent no-op) - the one seam gfx/image.c's
// generic draw_image() needs; the backbuffer array itself and every other
// backbuffer helper (bb_fill_rect) stay private to window.c.
void bb_put_pixel(u32 x, u32 y, u32 color);

#pragma GCC visibility pop
