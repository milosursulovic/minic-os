#pragma once
#include "../core.h"
#include "../window.h"

// First real ring3-side GUI toolkit piece (Faza II point 21) - a Button
// widget on top of the window/font/mouse syscalls. Split out of the
// former single gui_toolkit.h.

typedef struct {
    int window_id;
    u32 x, y, width, height;  // body-local rect, passed to window_id at init
    char* label;
    u32 normal_color, pressed_color, label_color;
    bool was_down;  // last-poll left-button state, for click edge detection
    bool last_rendered_pressed;  // what button_draw() last actually drew -
                                  // button_poll() only redraws on a change
} button;

static __attribute__((unused)) void button_draw(button* self, bool pressed) {
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

static __attribute__((unused)) void button_init(button* self, int window_id, u32 x, u32 y, u32 width, u32 height,
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
    self->last_rendered_pressed = false;
}

// Polls real mouse + window state (both live, not cached) and redraws the
// button pressed/normal - but ONLY when that visual state actually
// changes since the last poll. button_draw() erases-then-redraws the
// label as two separate syscalls/compositor_redraw() passes; calling it
// on every poll of an unthrottled forever-loop (desktop_shell.c has no
// yield/sleep syscall to pace itself with) made the label visibly blink
// continuously on a real display, even once compositor_redraw() itself
// stopped producing torn frames - a real bug, not the same one. Returns
// true exactly once per press-and-hold - on the down-transition while the
// cursor is inside the button - not on every poll while held, and not on
// release. No multi-window click arbitration yet (whichever window is on
// top doesn't matter here) - a real Window Server would gate this on
// focus/z-order too; that's still point 18's open "focus" item, not this
// widget's job.
static __attribute__((unused)) bool button_poll(button* self) {
    // TEMPORARY diagnostic guard - see gt_debug_print's own comment
    // (proc/gui_toolkit/core.h) and [[project_button_poll_crash_bug]]. A
    // real button is always a static/local variable's address within
    // this program's own private region - real ASLR (kernel/lib/rand.h)
    // now randomizes load_vaddr within [0x80000000, 0x80000000 +
    // ASLR_SLOTS*4096), so the upper bound here is derived, not a guess:
    // max load_vaddr (0x80000000 + 511*4096 = 0x801FF000) + the 0x20000
    // stack gap every spawn call site uses (0x8021F000) + one 4096-byte
    // stack page (0x80220000) - anything outside that range means `self`
    // itself got corrupted somewhere between being computed at the call
    // site and landing here, not a bug in *this* function. Logs and
    // bails out instead of dereferencing garbage, so the kernel survives
    // long enough to read the log.
    if ((u64) self < 0x80000000 || (u64) self > 0x80220000) {
        gt_debug_print("button_poll: BAD self ptr 0x", (u64) self);
        return false;
    }
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

    bool pressed = inside && left_down;
    if (pressed != self->last_rendered_pressed) {
        button_draw(self, pressed);
        self->last_rendered_pressed = pressed;
    }
    return clicked;
}
