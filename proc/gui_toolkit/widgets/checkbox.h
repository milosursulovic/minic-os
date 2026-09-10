#pragma once
#include "../core.h"
#include "../window.h"

// Checkbox - a real second interaction model beyond Button's momentary
// press: a persistent toggled boolean, flipped once per real click.
// Mouse-hit-test/edge-detection shape is copied from button_poll() (same
// gt_window_query+gt_mouse_query calls, same self-pointer sanity guard) -
// this codebase's own established convention is per-widget copies, not a
// shared base type, since every widget here is a tiny, self-contained
// struct with no inheritance mechanism in C worth building for two types.
// Split out of the former single gui_toolkit.h.

typedef struct {
    int window_id;
    u32 x, y, size;  // square box, body-local rect
    u32 box_color, check_color, bg_color;
    bool checked;
    bool was_down;
} checkbox;

static __attribute__((unused)) void checkbox_draw(checkbox* self) {
    gt_window_fill_rect_args outer;
    outer.id = self->window_id;
    outer.x = self->x;
    outer.y = self->y;
    outer.width = self->size;
    outer.height = self->size;
    outer.color = self->box_color;
    gt_syscall(30, (u64) &outer, 0, 0);

    // Inset by 2px on every side - an outline when unchecked (bg_color
    // inset over the box_color border), a solid fill when checked.
    u32 inset = self->size > 4 ? 2 : 0;
    gt_window_fill_rect_args inner;
    inner.id = self->window_id;
    inner.x = self->x + inset;
    inner.y = self->y + inset;
    inner.width = self->size - (inset * 2);
    inner.height = self->size - (inset * 2);
    inner.color = self->checked ? self->check_color : self->bg_color;
    gt_syscall(30, (u64) &inner, 0, 0);
}

static __attribute__((unused)) void checkbox_init(checkbox* self, int window_id, u32 x, u32 y, u32 size,
                            u32 box_color, u32 check_color, u32 bg_color, bool initial_checked) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->size = size;
    self->box_color = box_color;
    self->check_color = check_color;
    self->bg_color = bg_color;
    self->checked = initial_checked;
    self->was_down = false;
    checkbox_draw(self);
}

// Returns true exactly once per real click (down-transition while the
// cursor is inside) - same edge-detection shape as button_poll(), but
// flips self->checked internally FIRST and always redraws on a real
// click, since (unlike a Button) the visual state genuinely changed and
// must persist, not just reflect "currently held down."
static __attribute__((unused)) bool checkbox_poll(checkbox* self) {
    if ((u64) self < 0x80000000 || (u64) self > 0x80220000) {
        gt_debug_print("checkbox_poll: BAD self ptr 0x", (u64) self);
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
    bool inside = (u32) mouse_x >= screen_x && (u32) mouse_x < screen_x + self->size
        && (u32) mouse_y >= screen_y && (u32) mouse_y < screen_y + self->size;
    bool left_down = (buttons & 1) != 0;
    bool clicked = inside && left_down && !self->was_down;
    self->was_down = left_down;

    if (clicked) {
        self->checked = !self->checked;
        checkbox_draw(self);
    }
    return clicked;
}
