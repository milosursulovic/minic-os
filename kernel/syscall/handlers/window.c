#include "window.h"
#include "../../gfx/window/window.h"
#include "../../drivers/mouse/mouse.h"
#include "../../drivers/vbe/vbe.h"

typedef struct __attribute__((packed)) {
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    u32 body_color;
    u32 title_color;
} window_create_args;

typedef struct __attribute__((packed)) {
    int id;
    u32 x;
    u32 y;
    u32 width;
    u32 height;
    u32 color;
} window_fill_rect_args;

typedef struct __attribute__((packed)) {
    int id;
    u32 x;
    u32 y;
    u32 fg_color;
    u32 bg_color;
    char* text;
} window_draw_text_args;

typedef struct __attribute__((packed)) {
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    u32 body_color;
} window_create_borderless_args;

bool syscall_window(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    if (num == 26) {
        if (!g_fb_enabled) {
            vbe_init(800, 600);
        }
        window_create_args* args = (window_create_args*) a1;
        int id = window_create(args->x, args->y, args->width, args->height,
                                args->body_color, args->title_color);
        if (id < 0) {
            *result = (u64) -1;
            return true;
        }
        compositor_redraw();
        *result = (u64) id;
        return true;
    }
    if (num == 27) {
        bool ok = window_move((int) a1, (i32) a2, (i32) a3);
        if (!ok) {
            *result = (u64) -1;
            return true;
        }
        compositor_redraw();
        *result = 0;
        return true;
    }
    if (num == 28) {
        bool ok = window_raise((int) a1);
        if (!ok) {
            *result = (u64) -1;
            return true;
        }
        compositor_redraw();
        *result = 0;
        return true;
    }
    if (num == 29) {
        bool ok = window_close((int) a1);
        if (!ok) {
            *result = (u64) -1;
            return true;
        }
        compositor_redraw();
        *result = 0;
        return true;
    }
    if (num == 30) {
        window_fill_rect_args* args = (window_fill_rect_args*) a1;
        bool ok = window_fill_content_rect(args->id, args->x, args->y, args->width,
                                            args->height, args->color);
        if (!ok) {
            *result = (u64) -1;
            return true;
        }
        compositor_redraw();
        *result = 0;
        return true;
    }
    if (num == 31) {
        mouse_init();  // idempotent - real init happens once, safe to call every time
        u64 packed = ((u64) (u32) g_mouse_x & 0xFFFF)
            | (((u64) (u32) g_mouse_y & 0xFFFF) << 16)
            | ((u64) g_mouse_buttons << 32);
        *result = packed;
        return true;
    }
    if (num == 32) {
        window_draw_text_args* args = (window_draw_text_args*) a1;
        bool ok = window_draw_text(args->id, args->x, args->y, args->text,
                                    args->fg_color, args->bg_color);
        if (!ok) {
            *result = (u64) -1;
            return true;
        }
        compositor_redraw();
        *result = 0;
        return true;
    }
    if (num == 33) {
        i32 body_x, body_y;
        u32 body_width, body_height;
        bool ok = window_query((int) a1, &body_x, &body_y, &body_width, &body_height);
        if (!ok) {
            *result = (u64) -1;
            return true;
        }
        u64 packed = ((u64) (u32) body_x & 0xFFFF)
            | (((u64) (u32) body_y & 0xFFFF) << 16)
            | ((u64) body_width & 0xFFFF) << 32
            | (((u64) body_height & 0xFFFF) << 48);
        *result = packed;
        return true;
    }
    if (num == 34) {
        if (!g_fb_enabled) {
            vbe_init(800, 600);
        }
        window_create_borderless_args* args = (window_create_borderless_args*) a1;
        int id = window_create_borderless(args->x, args->y, args->width, args->height,
                                           args->body_color);
        if (id < 0) {
            *result = (u64) -1;
            return true;
        }
        compositor_redraw();
        *result = (u64) id;
        return true;
    }
    if (num == 58) {
        // register_terminal_window (window id via a1) - self-registration
        // convenience, same permissive tone as syscall 49's sys_setuid:
        // no check that the caller actually owns/created that window id.
        g_terminal_window_id = (int) a1;
        *result = 0;
        return true;
    }
    if (num == 69) {
        bool ok = window_focus((int) a1);
        *result = (u64) ok;
        return true;
    }
    if (num == 70) {
        int key = window_pop_key((int) a1);
        *result = (u64) key;
        return true;
    }
    if (num == 94) {
        bool ok = window_draw_wallpaper((int) a1);
        *result = (u64) ok;
        return true;
    }
    return false;
}
