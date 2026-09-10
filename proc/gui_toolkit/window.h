#pragma once
#include "core.h"

// Window/mouse/focus wrappers - syscalls 26-34, 31, 58, 69, 70. Split out
// of the former single gui_toolkit.h.

typedef struct __attribute__((packed)) {
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    u32 body_color;
    u32 title_color;
} gt_window_create_args;

typedef struct __attribute__((packed)) {
    int id;
    u32 x;
    u32 y;
    u32 width;
    u32 height;
    u32 color;
} gt_window_fill_rect_args;

typedef struct __attribute__((packed)) {
    int id;
    u32 x;
    u32 y;
    u32 fg_color;
    u32 bg_color;
    char* text;
} gt_window_draw_text_args;

typedef struct __attribute__((packed)) {
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    u32 body_color;
} gt_window_create_borderless_args;

// Ordinary bordered window - same as ring3prog.c's own window_create()
// wrapper, duplicated here so this toolkit stays self-contained. Returns
// -1 on failure.
static __attribute__((unused)) int gt_window_create(i32 x, i32 y, u32 width, u32 height, u32 body_color, u32 title_color) {
    gt_window_create_args args;
    args.x = x;
    args.y = y;
    args.width = width;
    args.height = height;
    args.body_color = body_color;
    args.title_color = title_color;
    u64 result = gt_syscall(26, (u64) &args, 0, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

// Returns false if id is invalid. body_x/body_y/body_width/body_height are
// screen coordinates/dims of the window's body (past the titlebar) -
// matches window_fill_content_rect/window_draw_text's own coordinate
// origin, so callers here never need to know TITLEBAR_HEIGHT.
static __attribute__((unused)) bool gt_window_query(int id, i32* body_x, i32* body_y, u32* body_width, u32* body_height) {
    u64 packed = gt_syscall(33, (u64) id, 0, 0);
    if (packed == (u64) -1) {
        return false;
    }
    *body_x = (i32) (packed & 0xFFFF);
    *body_y = (i32) ((packed >> 16) & 0xFFFF);
    *body_width = (u32) ((packed >> 32) & 0xFFFF);
    *body_height = (u32) ((packed >> 48) & 0xFFFF);
    return true;
}

static __attribute__((unused)) void gt_mouse_query(i32* x, i32* y, u8* buttons) {
    u64 packed = gt_syscall(31, 0, 0, 0);
    *x = (i32) (packed & 0xFFFF);
    *y = (i32) ((packed >> 16) & 0xFFFF);
    *buttons = (u8) ((packed >> 32) & 0xFF);
}

// Same slot/z-order rules as window_create, minus a titlebar - see
// kernel/gfx/window.h's window_create_borderless(). Returns -1 on failure.
static __attribute__((unused)) int gt_window_create_borderless(i32 x, i32 y, u32 width, u32 height, u32 body_color) {
    gt_window_create_borderless_args args;
    args.x = x;
    args.y = y;
    args.width = width;
    args.height = height;
    args.body_color = body_color;
    u64 result = gt_syscall(34, (u64) &args, 0, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

static __attribute__((unused)) bool gt_window_close(int id) {
    return gt_syscall(29, (u64) id, 0, 0) != 0;
}

// Tells the kernel "this window id is the terminal" so the console
// shell's `exit` command can close it later.
static __attribute__((unused)) void gt_register_terminal_window(int window_id) {
    gt_syscall(58, (u64) window_id, 0, 0);
}

// Real window focus (Faza II point 18) - wraps syscall 69. A window can
// request its own focus directly (a real "grab focus on open" pattern),
// not just receive it via a real mouse click.
static __attribute__((unused)) bool gt_focus_window(int window_id) {
    return gt_syscall(69, (u64) window_id, 0, 0) != 0;
}

// Returns the next queued keystroke for window_id if it's the currently
// focused window, else -1 - wraps syscall 70. Real ASCII: printable
// chars via the same g_scancode_table every console keystroke uses,
// '\n' for Enter, 0x08 for Backspace.
static __attribute__((unused)) int gt_read_key(int window_id) {
    return (int) gt_syscall(70, (u64) window_id, 0, 0);
}

// Draws the real embedded default wallpaper image (kernel/gfx/wallpaper/)
// into window_id's content buffer - wraps syscall 94.
static __attribute__((unused)) bool gt_window_draw_wallpaper(int window_id) {
    return gt_syscall(94, (u64) window_id, 0, 0) != 0;
}
