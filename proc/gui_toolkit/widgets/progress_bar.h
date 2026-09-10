#pragma once
#include "../core.h"
#include "../window.h"

// ProgressBar - non-interactive, no poll function: bg_color fills the
// full width, fill_color overlays a width scaled to percent (0-100,
// clamped). Same two-rect technique as checkbox/radio_button, just no
// mouse/click handling at all. Split out of the former single
// gui_toolkit.h.

typedef struct {
    int window_id;
    u32 x, y, width, height;
    u32 bg_color, fill_color;
    u32 percent;  // 0-100
} progress_bar;

static __attribute__((unused)) void progress_bar_draw(progress_bar* self) {
    gt_window_fill_rect_args bg;
    bg.id = self->window_id;
    bg.x = self->x;
    bg.y = self->y;
    bg.width = self->width;
    bg.height = self->height;
    bg.color = self->bg_color;
    gt_syscall(30, (u64) &bg, 0, 0);

    u32 fill_width = (self->width * self->percent) / 100;
    if (fill_width > 0) {
        gt_window_fill_rect_args fill;
        fill.id = self->window_id;
        fill.x = self->x;
        fill.y = self->y;
        fill.width = fill_width;
        fill.height = self->height;
        fill.color = self->fill_color;
        gt_syscall(30, (u64) &fill, 0, 0);
    }
}

static __attribute__((unused)) void progress_bar_init(progress_bar* self, int window_id, u32 x, u32 y,
                                u32 width, u32 height, u32 bg_color, u32 fill_color,
                                u32 initial_percent) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;
    self->bg_color = bg_color;
    self->fill_color = fill_color;
    self->percent = initial_percent > 100 ? 100 : initial_percent;
    progress_bar_draw(self);
}

static __attribute__((unused)) void progress_bar_set_percent(progress_bar* self, u32 percent) {
    self->percent = percent > 100 ? 100 : percent;
    progress_bar_draw(self);
}
