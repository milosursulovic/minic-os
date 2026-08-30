#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

#define WINDOW_SLOTS 8
#define TITLEBAR_HEIGHT 20
#define WINDOW_BACKGROUND_COLOR 0x00202020u

// A maximized window fills the screen minus this much at the bottom, so
// desktop_shell.c's taskbar (its own separate TASKBAR_HEIGHT, currently
// also 20 - coincidence, not shared, since the compositor doesn't
// otherwise know the taskbar exists as a concept) stays reachable while
// something is maximized.
#define MAXIMIZE_TASKBAR_RESERVE 20

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
    // "Shaded" (window-shade minimize) - the titlebar (with its icons)
    // still draws and is still draggable, only the body is skipped.
    bool minimized;
    // Filling the screen (minus the taskbar) - restore_x/y/width/height
    // hold the pre-maximize bounds, meaningful only while true.
    bool maximized;
    i32 restore_x;
    i32 restore_y;
    u32 restore_width;
    u32 restore_height;
} window;

// Set by syscall (register_terminal_window) the moment
// proc/apps/terminal/terminal.c creates its own window - lets the kernel
// console shell's `exit` command (shell/shell/shell.c) close it without
// fragile dimension-matching against g_windows[]. -1 = no terminal
// window currently open.
extern int g_terminal_window_id;

// Real window focus (Faza II point 18) - -1 means the console shell/
// full-screen editor owns the keyboard, exactly this kernel's only
// behavior before this existed. Set by a real mouse click (a non-
// borderless window's title+body, via compositor_handle_mouse()) or a
// window requesting its own focus (window_focus(), syscall 69) - a
// click on a borderless window (the wallpaper/taskbar - the only two
// that exist) clears this back to -1.
extern int g_focused_window_id;
bool window_focus(int id);

// A single shared keystroke queue for whichever window currently has
// focus - one queue, not per-window, since only one window can ever be
// focused at a time. Populated by kernel/isr/isr.c's keyboard IRQ
// handler only while g_focused_window_id >= 0 (the console/editor path
// is completely unchanged otherwise). window_pop_key() refuses to
// return anything to a caller whose window_id isn't the currently
// focused one - a window can only ever read keys typed while IT was
// focused, never another window's or the console's.
#define WINDOW_KEY_QUEUE_SIZE 16
bool window_push_key(char c);
int window_pop_key(int window_id);  // returns the char, or -1 if not focused/empty

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
// Same bounds checks as window_create (content-buffer cap, fits on
// screen at the window's CURRENT x/y) - refuses if the window is
// currently maximized (restore it first) or the result would be smaller
// than a usable minimum. A resized window shows more/less of its
// existing content buffer - it does not ask the owning app to redraw
// (see kernel/gfx/window/window.c's compositor_handle_mouse() for why
// that's a deliberate, documented scope limit, not an oversight).
bool window_resize(int id, u32 width, u32 height);
bool window_close(int id);
// Moves id to the top of the z-order - the compositor draws it last.
bool window_raise(int id);
// Drives titlebar-icon clicks (close/minimize/maximize), the bottom-
// right resize handle, and titlebar-drag-to-move - real mouse
// interaction, entirely kernel-side (no ring3 syscall involved). Call
// this unconditionally on every timer tick (kernel/isr/isr.c); it tracks
// button edges itself. Returns true if any window's on-screen state
// actually changed this tick (moved/resized/closed/(un)minimized/
// (un)maximized/raised), so the caller can decide whether a redraw is
// warranted - same idea as the existing cursor-moved check.
bool compositor_handle_mouse(void);
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
