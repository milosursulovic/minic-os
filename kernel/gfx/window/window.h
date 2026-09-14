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
    // Filling the ENTIRE screen (no taskbar notch, no titlebar drawn or
    // reserved) - real desktop fullscreen semantics (Faza II point 18),
    // toggled via the F11 hotkey (kernel/isr/isr.c), never by mouse (no
    // titlebar exists to click while fullscreen). Shares restore_x/y/
    // width/height with `maximized` above - a window is never both at
    // once (window_fullscreen_toggle() clears `maximized` on entry
    // instead of stacking a second restore point; see its own comment).
    bool fullscreen;
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

// A single shared input event queue for whichever window currently has
// focus - one queue, not per-window, since only one window can ever be
// focused at a time. Populated by kernel/isr/isr.c's keyboard/mouse IRQ
// handlers only while g_focused_window_id >= 0 (the console/editor path
// is completely unchanged otherwise). window_pop_event() refuses to
// return anything to a caller whose window_id isn't the currently
// focused one - a window can only ever read events raised while IT was
// focused, never another window's or the console's.
//
// Faza II point 17 (real input event queue, replacing ad hoc global-state
// polling): a real tagged event, not just a bare character - carries the
// raw scancode + live modifier state (for a hotkey-style consumer) and
// real mouse button press/release edges + wheel notches alongside
// keystrokes, all through the one queue.
typedef enum {
    INPUT_EVENT_KEY_DOWN,
    INPUT_EVENT_MOUSE_BUTTON,
    INPUT_EVENT_MOUSE_WHEEL
} input_event_type;

// modifiers bit0=shift bit1=ctrl bit2=alt
#define INPUT_MODIFIER_SHIFT 0x01
#define INPUT_MODIFIER_CTRL 0x02
#define INPUT_MODIFIER_ALT 0x04

// Packed so its layout is byte-identical to proc/gui_toolkit/window.h's
// own mirrored gt_input_event_t - this struct crosses the syscall 100
// boundary by raw pointer (a ring3-supplied buffer the kernel writes
// into directly, same pattern every other *_args struct here already
// uses), so both sides must agree on field offsets exactly. `type` is a
// plain u8 (an input_event_type value), not the enum itself - no other
// struct in this codebase puts a C enum directly in a boundary-crossing
// struct, and a fixed-width field sidesteps relying on both independently
// compiled sides picking the identical enum backing size.
typedef struct __attribute__((packed)) {
    u8 type;              // an input_event_type value
    char ascii;        // KEY_DOWN: shift-aware mapped character (0 if non-printable)
    u8 scancode;        // KEY_DOWN: raw scancode
    u8 modifiers;        // KEY_DOWN: live modifier bits at press time
    u8 button;            // MOUSE_BUTTON: 0=left 1=right 2=middle
    bool pressed;        // MOUSE_BUTTON: true=press false=release (a real edge, not polled state)
    i32 wheel_delta;    // MOUSE_WHEEL: signed notch count (positive = up)
} input_event_t;

#define WINDOW_KEY_QUEUE_SIZE 16
bool window_push_event(input_event_t evt);
bool window_pop_event(int window_id, input_event_t* out);  // false if not focused/empty
// Compatibility wrapper over window_pop_event() for callers that only
// want keystrokes (proc/gui_toolkit's text_box, gt_read_key) - silently
// skips any non-KEY_DOWN event at the front of the queue and returns the
// next real key's ascii, or -1 if not focused/empty.
int window_pop_key(int window_id);

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
// Real desktop fullscreen toggle (Faza II point 18) - see the
// `fullscreen` field's own comment. Public (unlike the mouse-only
// maximize_toggle(), static in window.c) because kernel/isr/isr.c's
// new F11 hotkey is the only way to trigger this - no titlebar icon
// exists once fullscreen, so there's nothing to click to reverse it
// without a keyboard path. False if id is out of range/unused.
bool window_fullscreen_toggle(int id);
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
// Copies the embedded default wallpaper image (kernel/gfx/wallpaper/
// wallpaper.h) into window id's content buffer at body-local (0,0),
// clipped to the body/image bounds (same clipping shape as
// window_fill_content_rect). Real IMAGE_TRANSPARENT pixels are skipped,
// same convention as image.h's own draw_image().
bool window_draw_wallpaper(int id);
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
