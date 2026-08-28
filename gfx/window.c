// A real window table + Z-order stack + compositor on top of drivers/vbe.c's
// framebuffer. A window's body is either a flat body_color placeholder or,
// once something draws into it, a real per-window pixel buffer an
// application controls.

#include "window.h"
#include "../drivers/vbe.h"
#include "font.h"
#include "../lib/strings.h"

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

static void bb_put_pixel(u32 x, u32 y, u32 color) {
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
    int id = 0;
    while (id < WINDOW_SLOTS && g_windows[id].used) {
        id = id + 1;
    }
    if (id >= WINDOW_SLOTS) {
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

    g_window_zorder[g_window_zorder_count] = id;
    g_window_zorder_count = g_window_zorder_count + 1;
    return id;
}

int window_create_borderless(i32 x, i32 y, u32 width, u32 height, u32 body_color) {
    if (width > WINDOW_CONTENT_MAX_WIDTH || height > WINDOW_CONTENT_MAX_HEIGHT) {
        return -1;
    }
    if (!fits_on_screen(x, y, width, height)) {
        return -1;
    }
    int id = 0;
    while (id < WINDOW_SLOTS && g_windows[id].used) {
        id = id + 1;
    }
    if (id >= WINDOW_SLOTS) {
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

    g_window_zorder[g_window_zorder_count] = id;
    g_window_zorder_count = g_window_zorder_count + 1;
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

bool window_close(int id) {
    if (id < 0 || id >= WINDOW_SLOTS || !g_windows[id].used) {
        return false;
    }
    int zi = zorder_index_of(id);
    while (zi < g_window_zorder_count - 1) {
        g_window_zorder[zi] = g_window_zorder[zi + 1];
        zi = zi + 1;
    }
    g_window_zorder_count = g_window_zorder_count - 1;
    g_windows[id].used = false;
    g_windows[id].has_content = false;
    return true;
}

bool window_raise(int id) {
    if (id < 0 || id >= WINDOW_SLOTS || !g_windows[id].used) {
        return false;
    }
    int zi = zorder_index_of(id);
    while (zi < g_window_zorder_count - 1) {
        g_window_zorder[zi] = g_window_zorder[zi + 1];
        zi = zi + 1;
    }
    g_window_zorder[g_window_zorder_count - 1] = id;
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

void compositor_redraw(void) {
    bb_fill_rect(0, 0, g_fb_width, g_fb_height, WINDOW_BACKGROUND_COLOR);
    int i = 0;
    while (i < g_window_zorder_count) {
        int id = g_window_zorder[i];
        window* w = &g_windows[id];
        if (!w->borderless) {
            bb_fill_rect((u32) w->x, (u32) w->y, w->width, TITLEBAR_HEIGHT, w->title_color);
        }
        if (w->has_content) {
            draw_window_content(w, id);
        } else {
            bb_fill_rect((u32) w->x, window_body_screen_y(w), w->width, window_body_height(w),
                         w->body_color);
        }
        i = i + 1;
    }

    // Publish: one tight, branch-light pass from the back buffer to the
    // real MMIO framebuffer - the only part of a redraw an external
    // observer can actually catch mid-flight, and now the smallest
    // possible window for that. cli/sti (save/restore IF, not a bare
    // pair, so this is correct regardless of the caller's own state)
    // additionally blocks a guest-side timer preemption from landing
    // here too - belt and suspenders, not a substitute for the back
    // buffer itself.
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    u32 y = 0;
    while (y < g_fb_height) {
        u32 x = 0;
        while (x < g_fb_width) {
            fb_put_pixel(x, y, g_backbuffer[y * COMPOSITOR_BACKBUFFER_WIDTH + x]);
            x = x + 1;
        }
        y = y + 1;
    }
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}
