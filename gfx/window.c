// A real window table + Z-order stack + compositor on top of drivers/vbe.c's
// framebuffer. A window's body is either a flat body_color placeholder or,
// once something draws into it, a real per-window pixel buffer an
// application controls.

#include "window.h"
#include "../drivers/vbe.h"

window g_windows[WINDOW_SLOTS];
int g_window_zorder[WINDOW_SLOTS];
int g_window_zorder_count;
u32 g_window_content[WINDOW_SLOTS][WINDOW_CONTENT_MAX_WIDTH * WINDOW_CONTENT_MAX_HEIGHT];

static bool fits_on_screen(i32 x, i32 y, u32 width, u32 height) {
    if (x < 0 || y < 0) {
        return false;
    }
    if ((u32) x + width > g_fb_width || (u32) y + height > g_fb_height) {
        return false;
    }
    return true;
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
    u32 body_height = g_windows[id].height - TITLEBAR_HEIGHT;
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

static void draw_window_content(window* w, int id) {
    u32 body_height = w->height - TITLEBAR_HEIGHT;
    u32 row = 0;
    while (row < body_height) {
        u32 col = 0;
        while (col < w->width) {
            u32 color = g_window_content[id][row * WINDOW_CONTENT_MAX_WIDTH + col];
            fb_put_pixel((u32) w->x + col, (u32) w->y + TITLEBAR_HEIGHT + row, color);
            col = col + 1;
        }
        row = row + 1;
    }
}

void compositor_redraw(void) {
    fb_fill_rect(0, 0, g_fb_width, g_fb_height, WINDOW_BACKGROUND_COLOR);
    int i = 0;
    while (i < g_window_zorder_count) {
        int id = g_window_zorder[i];
        window* w = &g_windows[id];
        fb_fill_rect((u32) w->x, (u32) w->y, w->width, TITLEBAR_HEIGHT, w->title_color);
        if (w->has_content) {
            draw_window_content(w, id);
        } else {
            fb_fill_rect((u32) w->x, (u32) w->y + TITLEBAR_HEIGHT, w->width,
                         w->height - TITLEBAR_HEIGHT, w->body_color);
        }
        i = i + 1;
    }
}
