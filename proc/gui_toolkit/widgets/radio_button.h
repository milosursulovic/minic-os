#pragma once
#include "../core.h"
#include "../window.h"

// RadioButton - same click-edge-detection shape as checkbox, but
// exclusive within a group instead of independently toggled: every
// radio_button in a group shares one `int* selected` (the group's own
// current selection, -1 = none). No true circle primitive exists in this
// codebase (window_fill_rect - syscall 30 - only draws rectangles), so
// this renders as nested squares, same stated simplification checkbox
// already uses. Real radio semantics: a click always SELECTS this one
// (never deselects on a re-click of an already-selected radio) - the
// caller is responsible for redrawing the rest of the group on the same
// real click if a different radio was previously selected (this toolkit
// has no observer/event system - every widget's app owns its own redraw
// sequencing, same convention every existing widget here already
// follows). Split out of the former single gui_toolkit.h.

typedef struct {
    int window_id;
    u32 x, y, size;
    u32 ring_color, dot_color, bg_color;
    int group_index;   // this radio's own index within its group
    int* selected;      // shared across the whole group
    bool was_down;
} radio_button;

static __attribute__((unused)) void radio_button_draw(radio_button* self) {
    gt_window_fill_rect_args outer;
    outer.id = self->window_id;
    outer.x = self->x;
    outer.y = self->y;
    outer.width = self->size;
    outer.height = self->size;
    outer.color = self->ring_color;
    gt_syscall(30, (u64) &outer, 0, 0);

    u32 inset = self->size > 4 ? 2 : 0;
    gt_window_fill_rect_args inner;
    inner.id = self->window_id;
    inner.x = self->x + inset;
    inner.y = self->y + inset;
    inner.width = self->size - (inset * 2);
    inner.height = self->size - (inset * 2);
    inner.color = (*self->selected == self->group_index) ? self->dot_color : self->bg_color;
    gt_syscall(30, (u64) &inner, 0, 0);
}

static __attribute__((unused)) void radio_button_init(radio_button* self, int window_id, u32 x, u32 y, u32 size,
                               u32 ring_color, u32 dot_color, u32 bg_color,
                               int group_index, int* selected) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->size = size;
    self->ring_color = ring_color;
    self->dot_color = dot_color;
    self->bg_color = bg_color;
    self->group_index = group_index;
    self->selected = selected;
    self->was_down = false;
    radio_button_draw(self);
}

static __attribute__((unused)) bool radio_button_poll(radio_button* self) {
    if ((u64) self < 0x80000000 || (u64) self > 0x80220000) {
        gt_debug_print("radio_button_poll: BAD self ptr 0x", (u64) self);
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
        *self->selected = self->group_index;
        radio_button_draw(self);
    }
    return clicked;
}
