#pragma once
#include "../core.h"
#include "../window.h"

// Slider - a genuinely new interaction model, unlike every widget above:
// continuous click-and-drag-to-scrub, not click-edge-detection. Every
// poll, if the left button is down AND the cursor is anywhere inside the
// track rect, the value is recomputed directly from the cursor's
// horizontal position - clicking anywhere on the track jumps the value
// there (real, common slider UX - e.g. a media player's scrub bar), then
// holding and moving keeps updating it. Returns true only when the value
// actually CHANGES this poll, not on every poll while held, so callers
// can react to real changes cheaply (same "only redraw/print on a real
// change" discipline checkbox_poll/radio_button_poll already follow).
// Split out of the former single gui_toolkit.h.

typedef struct {
    int window_id;
    u32 x, y, width, height;  // track rect
    u32 track_color, handle_color;
    u32 min_value, max_value, value;
} slider;

#define SLIDER_HANDLE_WIDTH 6

static __attribute__((unused)) void slider_draw(slider* self) {
    gt_window_fill_rect_args track;
    track.id = self->window_id;
    track.x = self->x;
    track.y = self->y;
    track.width = self->width;
    track.height = self->height;
    track.color = self->track_color;
    gt_syscall(30, (u64) &track, 0, 0);

    u32 range = self->max_value - self->min_value;
    u32 usable_width = self->width > SLIDER_HANDLE_WIDTH ? self->width - SLIDER_HANDLE_WIDTH : 0;
    u32 handle_offset = range > 0 ? ((self->value - self->min_value) * usable_width) / range : 0;

    gt_window_fill_rect_args handle;
    handle.id = self->window_id;
    handle.x = self->x + handle_offset;
    handle.y = self->y;
    handle.width = SLIDER_HANDLE_WIDTH;
    handle.height = self->height;
    handle.color = self->handle_color;
    gt_syscall(30, (u64) &handle, 0, 0);
}

static __attribute__((unused)) void slider_init(slider* self, int window_id, u32 x, u32 y, u32 width, u32 height,
                          u32 track_color, u32 handle_color,
                          u32 min_value, u32 max_value, u32 initial_value) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;
    self->track_color = track_color;
    self->handle_color = handle_color;
    self->min_value = min_value;
    self->max_value = max_value;
    self->value = initial_value < min_value ? min_value : (initial_value > max_value ? max_value : initial_value);
    slider_draw(self);
}

static __attribute__((unused)) bool slider_poll(slider* self) {
    if ((u64) self < 0x80000000 || (u64) self > 0x80220000) {
        gt_debug_print("slider_poll: BAD self ptr 0x", (u64) self);
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
    if (!inside || !left_down) {
        return false;
    }

    u32 rel_x = (u32) mouse_x - screen_x;
    u32 range = self->max_value - self->min_value;
    u32 new_value = self->min_value + (rel_x * range) / self->width;
    if (new_value > self->max_value) {
        new_value = self->max_value;
    }
    if (new_value == self->value) {
        return false;
    }
    self->value = new_value;
    slider_draw(self);
    return true;
}
