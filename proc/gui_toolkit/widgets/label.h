#pragma once
#include "../core.h"
#include "../window.h"

// Label - real widget consolidating a pattern that was already duplicated
// (proc/apps/settings/settings.c's draw_static_label(), proc/apps/
// device_manager/device_manager.c's draw_row_text()): a non-interactive
// text draw, same gt_window_draw_text_args+syscall 32 both of those
// already used. label_set_text() is the real reason this is a struct and
// not a bare free function - a caller like settings.c's redraw_stats()
// needs to update the same on-screen text repeatedly, not just draw it
// once. Split out of the former single gui_toolkit.h.

typedef struct {
    int window_id;
    u32 x, y;
    char* text;
    u32 fg_color, bg_color;
} label;

static __attribute__((unused)) void label_draw(label* self) {
    gt_window_draw_text_args args;
    args.id = self->window_id;
    args.x = self->x;
    args.y = self->y;
    args.fg_color = self->fg_color;
    args.bg_color = self->bg_color;
    args.text = self->text;
    gt_syscall(32, (u64) &args, 0, 0);
}

static __attribute__((unused)) void label_init(label* self, int window_id, u32 x, u32 y,
                         char* text, u32 fg_color, u32 bg_color) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->text = text;
    self->fg_color = fg_color;
    self->bg_color = bg_color;
    label_draw(self);
}

// Updates the text and redraws immediately - callers own clearing any old
// text first if the new string is shorter (matches how every existing
// hand-rolled redraw in this codebase already handles this, e.g. settings.c's
// redraw_stats() fills its background rect before redrawing text into it).
static __attribute__((unused)) void label_set_text(label* self, char* text) {
    self->text = text;
    label_draw(self);
}
