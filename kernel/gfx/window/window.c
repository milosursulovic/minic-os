// A real window table + Z-order stack + compositor on top of drivers/vbe.c's
// framebuffer. A window's body is either a flat body_color placeholder or,
// once something draws into it, a real per-window pixel buffer an
// application controls.

#include "window.h"
#include "../../drivers/vbe/vbe.h"
#include "../../drivers/mouse/mouse.h"
#include "../font/font.h"
#include "../../lib/strings.h"
#include "../image/image.h"
#include "../cursor_image/cursor_image.h"

window g_windows[WINDOW_SLOTS];
int g_window_zorder[WINDOW_SLOTS];
int g_window_zorder_count;
u32 g_window_content[WINDOW_SLOTS][WINDOW_CONTENT_MAX_WIDTH * WINDOW_CONTENT_MAX_HEIGHT];

// Off-screen composite buffer, sized to the one mode this kernel ever
// uses (vbe_init(800,600) - see drivers/vbe.c). compositor_redraw() draws
// every window into this plain array first, then blits it to the real
// framebuffer in one tight pass at the end - drivers/vbe.c's fb_put_pixel
// touches live MMIO-backed video memory directly, so without a back
// buffer, a mid-redraw timer preemption (or, just as much, an external
// observer - a real display, or QEMU's own screendump - sampling
// asynchronously, which no amount of guest-side cli/sti can prevent)
// shows a torn frame: background color on rows the multi-stage,
// branchy draw hadn't reached yet, sitting above already-drawn ones.
// This is the "double buffering" piece of Faza II point 16 (Graphics
// subsystem, "kasnije") - pulled in now because without it, point 22's
// desktop shell doesn't render correctly on a live display, only when
// sampled between redraws by luck.
#define COMPOSITOR_BACKBUFFER_WIDTH 800
#define COMPOSITOR_BACKBUFFER_HEIGHT 600
static u32 g_backbuffer[COMPOSITOR_BACKBUFFER_WIDTH * COMPOSITOR_BACKBUFFER_HEIGHT];

void bb_put_pixel(u32 x, u32 y, u32 color) {
    if (x >= COMPOSITOR_BACKBUFFER_WIDTH || y >= COMPOSITOR_BACKBUFFER_HEIGHT) {
        return;
    }
    g_backbuffer[y * COMPOSITOR_BACKBUFFER_WIDTH + x] = color;
}

static void bb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    u32 row = 0;
    while (row < h) {
        u32 col = 0;
        while (col < w) {
            bb_put_pixel(x + col, y + row, color);
            col = col + 1;
        }
        row = row + 1;
    }
}

// Save/restore IF (not a bare cli/sti pair, so this is correct regardless
// of whether interrupts were already off in the caller) - used to make a
// critical section atomic with respect to the timer ISR's preemption.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

static bool fits_on_screen(i32 x, i32 y, u32 width, u32 height) {
    if (x < 0 || y < 0) {
        return false;
    }
    if ((u32) x + width > g_fb_width || (u32) y + height > g_fb_height) {
        return false;
    }
    return true;
}

// A borderless window has no titlebar drawn or reserved - the whole
// height is body, and its screen origin is its own (x,y) directly. A
// bordered window's body starts TITLEBAR_HEIGHT below (x,y). Every place
// that used to inline `height - TITLEBAR_HEIGHT` / `y + TITLEBAR_HEIGHT`
// goes through these two instead, so bordered-window behavior is
// unchanged and borderless windows fall out of the same code paths.
static u32 window_body_height(window* w) {
    return w->borderless ? w->height : w->height - TITLEBAR_HEIGHT;
}

static u32 window_body_screen_y(window* w) {
    return w->borderless ? (u32) w->y : (u32) w->y + TITLEBAR_HEIGHT;
}

// A freshly created window can land on a REUSED slot (window_close() only
// marks the slot free, it never touches g_window_content) - without this,
// a new window whose owner hasn't yet drawn over every single cell of its
// own content area would show the PREVIOUS occupant's leftover pixels
// wherever it hasn't (has_content=true makes draw_window_content() paint
// every cell in the buffer unconditionally, stale ones included). Real
// bug, found via desktop_shell.c's MENU dropdown: close the popup, spawn
// Terminal into the same freed slot, see the popup's old button labels
// baked into the new window.
static void clear_window_content(int id) {
    u32 i = 0;
    while (i < WINDOW_CONTENT_MAX_WIDTH * WINDOW_CONTENT_MAX_HEIGHT) {
        g_window_content[id][i] = 0;
        i = i + 1;
    }
}

int window_create(i32 x, i32 y, u32 width, u32 height, u32 body_color, u32 title_color) {
    if (height <= TITLEBAR_HEIGHT) {
        return -1;  // no room for a body at all
    }
    if (width > WINDOW_CONTENT_MAX_WIDTH || height - TITLEBAR_HEIGHT > WINDOW_CONTENT_MAX_HEIGHT) {
        return -1;
    }
    if (!fits_on_screen(x, y, width, height)) {
        return -1;
    }
    // Find-a-free-slot-then-mark-it-used is a real critical section: two
    // ring3 tasks can both call a window_create* syscall around the same
    // moment (e.g. at boot, desktop_shell.c and terminal.c both do) and
    // the timer ISR can preempt between the scan and the write below -
    // without this, both tasks can see the same slot as free and one's
    // window silently clobbers the other's (found via a real symptom: a
    // window that visually existed but never showed the content its own
    // owning task kept writing - a second task's window_create had
    // stomped the same slot after the fact).
    u64 saved_flags = disable_interrupts();
    int id = 0;
    while (id < WINDOW_SLOTS && g_windows[id].used) {
        id = id + 1;
    }
    if (id >= WINDOW_SLOTS) {
        restore_interrupts(saved_flags);
        return -1;
    }

    g_windows[id].used = true;
    g_windows[id].x = x;
    g_windows[id].y = y;
    g_windows[id].width = width;
    g_windows[id].height = height;
    g_windows[id].body_color = body_color;
    g_windows[id].title_color = title_color;
    g_windows[id].has_content = false;
    g_windows[id].borderless = false;
    g_windows[id].minimized = false;
    g_windows[id].maximized = false;
    clear_window_content(id);

    g_window_zorder[g_window_zorder_count] = id;
    g_window_zorder_count = g_window_zorder_count + 1;
    restore_interrupts(saved_flags);
    return id;
}

int window_create_borderless(i32 x, i32 y, u32 width, u32 height, u32 body_color) {
    if (width > WINDOW_CONTENT_MAX_WIDTH || height > WINDOW_CONTENT_MAX_HEIGHT) {
        return -1;
    }
    if (!fits_on_screen(x, y, width, height)) {
        return -1;
    }
    // Same slot-allocation race as window_create() above - see its comment.
    u64 saved_flags = disable_interrupts();
    int id = 0;
    while (id < WINDOW_SLOTS && g_windows[id].used) {
        id = id + 1;
    }
    if (id >= WINDOW_SLOTS) {
        restore_interrupts(saved_flags);
        return -1;
    }

    g_windows[id].used = true;
    g_windows[id].x = x;
    g_windows[id].y = y;
    g_windows[id].width = width;
    g_windows[id].height = height;
    g_windows[id].body_color = body_color;
    g_windows[id].title_color = 0;
    g_windows[id].has_content = false;
    g_windows[id].borderless = true;
    g_windows[id].minimized = false;
    g_windows[id].maximized = false;
    clear_window_content(id);

    g_window_zorder[g_window_zorder_count] = id;
    g_window_zorder_count = g_window_zorder_count + 1;
    restore_interrupts(saved_flags);
    return id;
}

static int zorder_index_of(int id) {
    int i = 0;
    while (i < g_window_zorder_count) {
        if (g_window_zorder[i] == id) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

bool window_move(int id, i32 x, i32 y) {
    if (id < 0 || id >= WINDOW_SLOTS || !g_windows[id].used) {
        return false;
    }
    if (!fits_on_screen(x, y, g_windows[id].width, g_windows[id].height)) {
        return false;
    }
    g_windows[id].x = x;
    g_windows[id].y = y;
    return true;
}

bool window_resize(int id, u32 width, u32 height) {
    if (id < 0 || id >= WINDOW_SLOTS || !g_windows[id].used) {
        return false;
    }
    if (g_windows[id].maximized) {
        return false;
    }
    if (g_windows[id].borderless) {
        if (width > WINDOW_CONTENT_MAX_WIDTH || height > WINDOW_CONTENT_MAX_HEIGHT) {
            return false;
        }
    } else {
        if (height <= TITLEBAR_HEIGHT) {
            return false;
        }
        if (width > WINDOW_CONTENT_MAX_WIDTH || height - TITLEBAR_HEIGHT > WINDOW_CONTENT_MAX_HEIGHT) {
            return false;
        }
    }
    if (!fits_on_screen(g_windows[id].x, g_windows[id].y, width, height)) {
        return false;
    }
    g_windows[id].width = width;
    g_windows[id].height = height;
    return true;
}

// window_close()/window_raise() both mutate the shared g_window_zorder
// array (a shift-then-place, not a single atomic write) - same real
// hazard window_create()'s own comment already documents for slot
// allocation, just newly reachable here too: compositor_handle_mouse()
// (kernel/isr/isr.c's timer tick) now calls both of these directly from
// INTERRUPT context, so without disabling interrupts, a ring3 task's own
// window_close/window_raise syscall (kernel/syscall/syscall.c, runs with
// interrupts enabled, preemptible) could be caught mid-shift by the timer
// ISR, which would then mutate the same array out from under it. Neither
// function had this real caller before this session (window_raise's only
// prior caller was an old demo trigger, window_close's real UI trigger -
// a titlebar close icon - didn't exist at all), so the race was never
// actually exercised until now.
bool window_close(int id) {
    if (id < 0 || id >= WINDOW_SLOTS || !g_windows[id].used) {
        return false;
    }
    u64 saved_flags = disable_interrupts();
    int zi = zorder_index_of(id);
    if (zi < 0) {
        // Not actually in the z-order - g_windows[id].used said otherwise,
        // a real inconsistency, not something to paper over by guessing.
        restore_interrupts(saved_flags);
        return false;
    }
    while (zi < g_window_zorder_count - 1) {
        g_window_zorder[zi] = g_window_zorder[zi + 1];
        zi = zi + 1;
    }
    g_window_zorder_count = g_window_zorder_count - 1;
    g_windows[id].used = false;
    g_windows[id].has_content = false;
    restore_interrupts(saved_flags);
    return true;
}

bool window_raise(int id) {
    if (id < 0 || id >= WINDOW_SLOTS || !g_windows[id].used) {
        return false;
    }
    u64 saved_flags = disable_interrupts();
    int zi = zorder_index_of(id);
    if (zi < 0) {
        restore_interrupts(saved_flags);
        return false;
    }
    while (zi < g_window_zorder_count - 1) {
        g_window_zorder[zi] = g_window_zorder[zi + 1];
        zi = zi + 1;
    }
    g_window_zorder[g_window_zorder_count - 1] = id;
    restore_interrupts(saved_flags);
    return true;
}

bool window_fill_content_rect(int id, u32 x, u32 y, u32 w, u32 h, u32 color) {
    if (id < 0 || id >= WINDOW_SLOTS || !g_windows[id].used) {
        return false;
    }
    u32 body_height = window_body_height(&g_windows[id]);
    u32 row = 0;
    while (row < h && y + row < body_height) {
        u32 col = 0;
        while (col < w && x + col < g_windows[id].width) {
            g_window_content[id][(y + row) * WINDOW_CONTENT_MAX_WIDTH + (x + col)] = color;
            col = col + 1;
        }
        row = row + 1;
    }
    g_windows[id].has_content = true;
    return true;
}

// Same clipping shape as window_fill_content_rect - body-local coordinates,
// clamped to the body size. Single line only, no wrapping (same limitation
// fb_draw_string has). Unsupported characters (see gfx/font.h) render blank.
bool window_draw_text(int id, u32 x, u32 y, const char* text, u32 fg, u32 bg) {
    if (id < 0 || id >= WINDOW_SLOTS || !g_windows[id].used) {
        return false;
    }
    u32 body_height = window_body_height(&g_windows[id]);
    u32 cursor = x;
    int i = 0;
    int len = strlen_(text);
    while (i < len) {
        u8 rows[FONT_GLYPH_HEIGHT];
        // Unsupported characters (font_get_glyph returns false) leave the
        // content buffer untouched at this cell - same as fb_draw_char.
        if (font_get_glyph(text[i], rows)) {
            u32 row = 0;
            while (row < FONT_GLYPH_HEIGHT && y + row < body_height) {
                u32 col = 0;
                while (col < FONT_GLYPH_WIDTH && cursor + col < g_windows[id].width) {
                    bool on = (rows[row] >> (FONT_GLYPH_WIDTH - 1 - col)) & 1;
                    g_window_content[id][(y + row) * WINDOW_CONTENT_MAX_WIDTH + (cursor + col)] =
                        on ? fg : bg;
                    col = col + 1;
                }
                row = row + 1;
            }
        }
        cursor = cursor + FONT_GLYPH_WIDTH + 1;
        i = i + 1;
    }
    g_windows[id].has_content = true;
    return true;
}

bool window_query(int id, i32* body_x, i32* body_y, u32* body_width, u32* body_height) {
    if (id < 0 || id >= WINDOW_SLOTS || !g_windows[id].used) {
        return false;
    }
    *body_x = g_windows[id].x;
    *body_y = (i32) window_body_screen_y(&g_windows[id]);
    *body_width = g_windows[id].width;
    *body_height = window_body_height(&g_windows[id]);
    return true;
}

static void draw_window_content(window* w, int id) {
    u32 body_height = window_body_height(w);
    u32 body_y = window_body_screen_y(w);
    u32 row = 0;
    while (row < body_height) {
        u32 col = 0;
        while (col < w->width) {
            u32 color = g_window_content[id][row * WINDOW_CONTENT_MAX_WIDTH + col];
            bb_put_pixel((u32) w->x + col, body_y + row, color);
            col = col + 1;
        }
        row = row + 1;
    }
}

// The cursor is a real image asset now (gfx/cursor_image.c), drawn by the
// generic gfx/image.c renderer - the compositor itself no longer knows
// anything about the cursor's shape, only that it draws "an image" at the
// live mouse position.
static void draw_cursor(void) {
    cursor_image_init();
    draw_image((u32) g_mouse_x, (u32) g_mouse_y, &g_cursor_image);
}

// Titlebar chrome: three small hand-drawn (not font, not PNG - these are
// fixed 12x12 UI glyphs, simplest to draw directly) icons, right-aligned
// in the titlebar. index_from_right: 0=close (rightmost), 1=maximize,
// 2=minimize (leftmost of the three). Shared by drawing and hit-testing
// (compositor_handle_mouse() below) so they can never drift apart.
#define TITLEBAR_ICON_SIZE 12
#define TITLEBAR_ICON_MARGIN 4
#define TITLEBAR_ICON_GAP 4
#define TITLEBAR_ICON_COLOR 0x00E0E0E0u
#define RESIZE_HANDLE_SIZE 10

static void icon_rect(window* w, int index_from_right, i32* out_x, i32* out_y) {
    *out_x = w->x + (i32) w->width - TITLEBAR_ICON_MARGIN
             - (index_from_right + 1) * TITLEBAR_ICON_SIZE
             - index_from_right * TITLEBAR_ICON_GAP;
    *out_y = w->y + (TITLEBAR_HEIGHT - TITLEBAR_ICON_SIZE) / 2;
}

static bool point_in_box(i32 px, i32 py, i32 bx, i32 by, u32 bw, u32 bh) {
    return px >= bx && px < bx + (i32) bw && py >= by && py < by + (i32) bh;
}

static void draw_close_icon(i32 x, i32 y) {
    int i = 0;
    while (i < TITLEBAR_ICON_SIZE) {
        bb_put_pixel((u32) (x + i), (u32) (y + i), TITLEBAR_ICON_COLOR);
        bb_put_pixel((u32) (x + i), (u32) (y + TITLEBAR_ICON_SIZE - 1 - i), TITLEBAR_ICON_COLOR);
        i = i + 1;
    }
}

static void draw_maximize_icon(i32 x, i32 y) {
    bb_fill_rect((u32) x, (u32) y, TITLEBAR_ICON_SIZE, 1, TITLEBAR_ICON_COLOR);
    bb_fill_rect((u32) x, (u32) (y + TITLEBAR_ICON_SIZE - 1), TITLEBAR_ICON_SIZE, 1, TITLEBAR_ICON_COLOR);
    bb_fill_rect((u32) x, (u32) y, 1, TITLEBAR_ICON_SIZE, TITLEBAR_ICON_COLOR);
    bb_fill_rect((u32) (x + TITLEBAR_ICON_SIZE - 1), (u32) y, 1, TITLEBAR_ICON_SIZE, TITLEBAR_ICON_COLOR);
}

static void draw_minimize_icon(i32 x, i32 y) {
    bb_fill_rect((u32) x, (u32) (y + TITLEBAR_ICON_SIZE - 2), TITLEBAR_ICON_SIZE, 2, TITLEBAR_ICON_COLOR);
}

static void draw_titlebar_icons(window* w) {
    i32 ix, iy;
    icon_rect(w, 0, &ix, &iy);
    draw_close_icon(ix, iy);
    icon_rect(w, 1, &ix, &iy);
    draw_maximize_icon(ix, iy);
    icon_rect(w, 2, &ix, &iy);
    draw_minimize_icon(ix, iy);
}

void compositor_redraw(void) {
    bb_fill_rect(0, 0, g_fb_width, g_fb_height, WINDOW_BACKGROUND_COLOR);
    int i = 0;
    while (i < g_window_zorder_count) {
        int id = g_window_zorder[i];
        window* w = &g_windows[id];
        if (!w->borderless) {
            bb_fill_rect((u32) w->x, (u32) w->y, w->width, TITLEBAR_HEIGHT, w->title_color);
            draw_titlebar_icons(w);
        }
        if (w->minimized) {
            // Shaded - only the titlebar (drawn above) shows.
        } else if (w->has_content) {
            draw_window_content(w, id);
        } else {
            bb_fill_rect((u32) w->x, window_body_screen_y(w), w->width, window_body_height(w),
                         w->body_color);
        }
        i = i + 1;
    }
    draw_cursor();

    // Publish: one tight, branch-light pass from the back buffer to the
    // real MMIO framebuffer - the only part of a redraw an external
    // observer can actually catch mid-flight, and now the smallest
    // possible window for that. Disabling interrupts additionally blocks
    // a guest-side timer preemption from landing here too - belt and
    // suspenders, not a substitute for the back buffer itself.
    u64 saved_flags = disable_interrupts();
    u32 y = 0;
    while (y < g_fb_height) {
        u32 x = 0;
        while (x < g_fb_width) {
            fb_put_pixel(x, y, g_backbuffer[y * COMPOSITOR_BACKBUFFER_WIDTH + x]);
            x = x + 1;
        }
        y = y + 1;
    }
    restore_interrupts(saved_flags);
}

// Real mouse-driven window interaction - entirely kernel-side, no ring3
// syscall involved (window x/y/width/height and the titlebar's own
// drawing already live here; the mouse's live position/buttons are
// kernel globals too - see kernel/gfx/window/window.h's
// compositor_handle_mouse() doc comment for why this needs no app-side
// changes at all). One global interaction state, not per-window - a
// single mouse can only manipulate one window at a time.
typedef enum { WM_IDLE, WM_DRAGGING, WM_RESIZING } wm_mode;

static wm_mode g_wm_mode = WM_IDLE;
static int g_wm_target = -1;
static i32 g_wm_offset_x;
static i32 g_wm_offset_y;
static i32 g_wm_resize_start_mouse_x;
static i32 g_wm_resize_start_mouse_y;
static u32 g_wm_resize_start_width;
static u32 g_wm_resize_start_height;
static u8 g_wm_prev_buttons;

#define WM_MIN_WIDTH 100
#define WM_MIN_HEIGHT (TITLEBAR_HEIGHT + 40)

static void maximize_toggle(window* w) {
    if (!w->maximized) {
        w->restore_x = w->x;
        w->restore_y = w->y;
        w->restore_width = w->width;
        w->restore_height = w->height;
        w->x = 0;
        w->y = 0;
        w->width = g_fb_width;
        w->height = g_fb_height - MAXIMIZE_TASKBAR_RESERVE;
        w->maximized = true;
    } else {
        w->x = w->restore_x;
        w->y = w->restore_y;
        w->width = w->restore_width;
        w->height = w->restore_height;
        w->maximized = false;
    }
}

bool compositor_handle_mouse(void) {
    bool changed = false;
    u8 buttons = g_mouse_buttons;
    bool left_down_now = (buttons & 1) != 0;
    bool left_down_before = (g_wm_prev_buttons & 1) != 0;

    if (left_down_now && !left_down_before) {
        // A fresh left-button press - hit-test topmost window first (the
        // z-order array's last entry draws last, i.e. on top).
        int zi = g_window_zorder_count - 1;
        while (zi >= 0) {
            int id = g_window_zorder[zi];
            window* w = &g_windows[id];
            if (!w->borderless) {
                i32 icon_x, icon_y;
                icon_rect(w, 0, &icon_x, &icon_y);
                if (point_in_box(g_mouse_x, g_mouse_y, icon_x, icon_y, TITLEBAR_ICON_SIZE, TITLEBAR_ICON_SIZE)) {
                    window_close(id);
                    changed = true;
                    break;
                }
                icon_rect(w, 1, &icon_x, &icon_y);
                if (point_in_box(g_mouse_x, g_mouse_y, icon_x, icon_y, TITLEBAR_ICON_SIZE, TITLEBAR_ICON_SIZE)) {
                    maximize_toggle(w);
                    window_raise(id);
                    changed = true;
                    break;
                }
                icon_rect(w, 2, &icon_x, &icon_y);
                if (point_in_box(g_mouse_x, g_mouse_y, icon_x, icon_y, TITLEBAR_ICON_SIZE, TITLEBAR_ICON_SIZE)) {
                    w->minimized = !w->minimized;
                    window_raise(id);
                    changed = true;
                    break;
                }
                if (!w->maximized && !w->minimized) {
                    i32 handle_x = w->x + (i32) w->width - RESIZE_HANDLE_SIZE;
                    i32 handle_y = w->y + (i32) w->height - RESIZE_HANDLE_SIZE;
                    if (point_in_box(g_mouse_x, g_mouse_y, handle_x, handle_y,
                                      RESIZE_HANDLE_SIZE, RESIZE_HANDLE_SIZE)) {
                        g_wm_mode = WM_RESIZING;
                        g_wm_target = id;
                        g_wm_resize_start_mouse_x = g_mouse_x;
                        g_wm_resize_start_mouse_y = g_mouse_y;
                        g_wm_resize_start_width = w->width;
                        g_wm_resize_start_height = w->height;
                        window_raise(id);
                        changed = true;
                        break;
                    }
                }
                if (point_in_box(g_mouse_x, g_mouse_y, w->x, w->y, w->width, TITLEBAR_HEIGHT)) {
                    g_wm_mode = WM_DRAGGING;
                    g_wm_target = id;
                    g_wm_offset_x = g_mouse_x - w->x;
                    g_wm_offset_y = g_mouse_y - w->y;
                    window_raise(id);
                    changed = true;
                    break;
                }
            }
            zi = zi - 1;
        }
    } else if (left_down_now && left_down_before) {
        if (g_wm_mode == WM_DRAGGING && g_wm_target >= 0 && g_windows[g_wm_target].used) {
            i32 new_x = g_mouse_x - g_wm_offset_x;
            i32 new_y = g_mouse_y - g_wm_offset_y;
            if (window_move(g_wm_target, new_x, new_y)) {
                changed = true;
            }
        } else if (g_wm_mode == WM_RESIZING && g_wm_target >= 0 && g_windows[g_wm_target].used) {
            i32 dx = g_mouse_x - g_wm_resize_start_mouse_x;
            i32 dy = g_mouse_y - g_wm_resize_start_mouse_y;
            i32 new_w = (i32) g_wm_resize_start_width + dx;
            i32 new_h = (i32) g_wm_resize_start_height + dy;
            if (new_w < WM_MIN_WIDTH) {
                new_w = WM_MIN_WIDTH;
            }
            if (new_h < WM_MIN_HEIGHT) {
                new_h = WM_MIN_HEIGHT;
            }
            if (window_resize(g_wm_target, (u32) new_w, (u32) new_h)) {
                changed = true;
            }
        }
    } else if (!left_down_now && left_down_before) {
        g_wm_mode = WM_IDLE;
        g_wm_target = -1;
    }

    g_wm_prev_buttons = buttons;
    return changed;
}
