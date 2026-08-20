#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

#define WINDOW_SLOTS 8
#define TITLEBAR_HEIGHT 20
#define WINDOW_BACKGROUND_COLOR 0x00202020u

typedef struct {
    bool used;
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    u32 body_color;
    u32 title_color;
} window;

extern window g_windows[WINDOW_SLOTS];
extern int g_window_zorder[WINDOW_SLOTS];
extern int g_window_zorder_count;

// Returns the new window's id (its g_windows[] slot), or -1 if out of slots
// or the requested bounds don't fit on screen. New windows land on top.
int window_create(i32 x, i32 y, u32 width, u32 height, u32 body_color, u32 title_color);
bool window_move(int id, i32 x, i32 y);
bool window_close(int id);
// Moves id to the top of the z-order - the compositor draws it last.
bool window_raise(int id);
// Fills the background, then draws every window bottom-to-top.
void compositor_redraw(void);

#pragma GCC visibility pop
