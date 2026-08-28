// First real ring3-side GUI toolkit piece (Faza II point 21) - a Button
// widget on top of the window/font/mouse syscalls. Self-contained (own
// syscall wrapper, own arg structs) so it can be #include-d by any future
// ring3 program, unlike every ring3 .c file so far which duplicates this
// stuff inline with zero sharing. Takes a plain window id (int), not a
// window* struct, so it has no dependency on whatever window type the
// including .c file defines.

#pragma once
#include "../types.h"

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

static u64 gt_syscall(u64 num, u64 arg1, u64 arg2, u64 arg3) {
    u64 result;
    register u64 r_num __asm__("rax") = num;
    register u64 r_arg1 __asm__("rdi") = arg1;
    register u64 r_arg2 __asm__("rsi") = arg2;
    register u64 r_arg3 __asm__("rdx") = arg3;
    __asm__ volatile("int $0x80"
                      : "+r"(r_num)
                      : "r"(r_arg1), "r"(r_arg2), "r"(r_arg3)
                      : "memory");
    result = r_num;
    return result;
}

// Returns false if id is invalid. body_x/body_y/body_width/body_height are
// screen coordinates/dims of the window's body (past the titlebar) -
// matches window_fill_content_rect/window_draw_text's own coordinate
// origin, so callers here never need to know TITLEBAR_HEIGHT.
static bool gt_window_query(int id, i32* body_x, i32* body_y, u32* body_width, u32* body_height) {
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

static void gt_mouse_query(i32* x, i32* y, u8* buttons) {
    u64 packed = gt_syscall(31, 0, 0, 0);
    *x = (i32) (packed & 0xFFFF);
    *y = (i32) ((packed >> 16) & 0xFFFF);
    *buttons = (u8) ((packed >> 32) & 0xFF);
}

typedef struct {
    int window_id;
    u32 x, y, width, height;  // body-local rect, passed to window_id at init
    char* label;
    u32 normal_color, pressed_color, label_color;
    bool was_down;  // last-poll left-button state, for click edge detection
} button;

static void button_draw(button* self, bool pressed) {
    u32 color = pressed ? self->pressed_color : self->normal_color;

    gt_window_fill_rect_args fill_args;
    fill_args.id = self->window_id;
    fill_args.x = self->x;
    fill_args.y = self->y;
    fill_args.width = self->width;
    fill_args.height = self->height;
    fill_args.color = color;
    gt_syscall(30, (u64) &fill_args, 0, 0);

    // Small left/vertical padding, not true centering - real text-width
    // measurement (label centering) is a natural next toolkit addition,
    // out of scope for a first Button.
    gt_window_draw_text_args text_args;
    text_args.id = self->window_id;
    text_args.x = self->x + 4;
    text_args.y = self->y + (self->height > 9 ? (self->height - 7) / 2 : 1);
    text_args.fg_color = self->label_color;
    text_args.bg_color = color;
    text_args.text = self->label;
    gt_syscall(32, (u64) &text_args, 0, 0);
}

static void button_init(button* self, int window_id, u32 x, u32 y, u32 width, u32 height,
                         char* label, u32 normal_color, u32 pressed_color, u32 label_color) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;
    self->label = label;
    self->normal_color = normal_color;
    self->pressed_color = pressed_color;
    self->label_color = label_color;
    self->was_down = false;
    button_draw(self, false);
}

// Polls real mouse + window state (both live, not cached) and redraws the
// button pressed/normal. Returns true exactly once per press-and-hold - on
// the down-transition while the cursor is inside the button - not on every
// poll while held, and not on release. No multi-window click arbitration
// yet (whichever window is on top doesn't matter here) - a real Window
// Server would gate this on focus/z-order too; that's still point 18's
// open "focus" item, not this widget's job.
static bool button_poll(button* self) {
    i32 win_x, win_y;
    u32 win_width, win_height;
    if (!gt_window_query(self->window_id, &win_x, &win_y, &win_width, &win_height)) {
        return false;
    }
    i32 mouse_x, mouse_y;
    u8 buttons;
    gt_mouse_query(&mouse_x, &mouse_y, &buttons);

    u32 screen_x = (u32) win_x + self->x;
    u32 screen_y = (u32) win_y + self->y;
    bool inside = (u32) mouse_x >= screen_x && (u32) mouse_x < screen_x + self->width
        && (u32) mouse_y >= screen_y && (u32) mouse_y < screen_y + self->height;
    bool left_down = (buttons & 1) != 0;
    bool clicked = inside && left_down && !self->was_down;
    self->was_down = left_down;

    button_draw(self, inside && left_down);
    return clicked;
}
