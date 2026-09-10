#pragma once
#include "../core.h"
#include "../window.h"

// ListView - real extraction of the row-select-with-highlight pattern
// already independently duplicated in proc/apps/file_manager/
// file_manager.c's redraw_all_rows()/g_row_btns[] and proc/apps/
// service_manager/service_manager.c's redraw_rows()/g_row_btns[]. This
// milestone adds the widget and demos it (ring3prog.c) - it deliberately
// does NOT retrofit either existing app to use it (real, separate,
// regression-risk-bearing follow-up, not bundled in here). Split out of
// the former single gui_toolkit.h.

#define LIST_VIEW_MAX_ROWS 16

typedef struct {
    int window_id;
    u32 x, y, width, row_height;
    int row_count;
    char* labels[LIST_VIEW_MAX_ROWS];  // caller-owned strings, not copied - same convention label already uses
    int selected_index;  // -1 = none
    u32 row_color, selected_color, label_color, bg_color;
    bool was_down;  // click-edge-detection state, same field every other widget here carries
} list_view;

static __attribute__((unused)) void list_view_draw_row(list_view* self, int row) {
    u32 y = self->y + (u32) (row * (int) self->row_height);
    gt_window_fill_rect_args bg;
    bg.id = self->window_id;
    bg.x = self->x;
    bg.y = y;
    bg.width = self->width;
    bg.height = self->row_height;
    bg.color = (row < self->row_count)
        ? (row == self->selected_index ? self->selected_color : self->row_color)
        : self->bg_color;
    gt_syscall(30, (u64) &bg, 0, 0);

    if (row < self->row_count) {
        gt_window_draw_text_args text_args;
        text_args.id = self->window_id;
        text_args.x = self->x;
        text_args.y = y;
        text_args.fg_color = self->label_color;
        text_args.bg_color = bg.color;
        text_args.text = self->labels[row];
        gt_syscall(32, (u64) &text_args, 0, 0);
    }
}

static __attribute__((unused)) void list_view_init(list_view* self, int window_id, u32 x, u32 y,
                             u32 width, u32 row_height, u32 row_color,
                             u32 selected_color, u32 label_color, u32 bg_color) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->width = width;
    self->row_height = row_height;
    self->row_count = 0;
    self->selected_index = -1;
    self->row_color = row_color;
    self->selected_color = selected_color;
    self->label_color = label_color;
    self->bg_color = bg_color;
    self->was_down = false;
}

// Replaces the row set and redraws everything - both the newly-populated
// rows AND any trailing rows that were populated before but aren't
// anymore, cleared to bg_color (same "clear stale slots" step file_manager.c's
// own redraw_all_rows() already does for its own row buttons).
static __attribute__((unused)) void list_view_set_rows(list_view* self, char** labels, int row_count) {
    self->row_count = row_count > LIST_VIEW_MAX_ROWS ? LIST_VIEW_MAX_ROWS : row_count;
    int i = 0;
    while (i < self->row_count) {
        self->labels[i] = labels[i];
        i = i + 1;
    }
    self->selected_index = -1;
    i = 0;
    while (i < LIST_VIEW_MAX_ROWS) {
        list_view_draw_row(self, i);
        i = i + 1;
    }
}

static __attribute__((unused)) int list_view_poll(list_view* self) {
    if ((u64) self < 0x80000000 || (u64) self > 0x80220000) {
        gt_debug_print("list_view_poll: BAD self ptr 0x", (u64) self);
        return -1;
    }
    i32 win_x, win_y;
    u32 win_width, win_height;
    if (!gt_window_query(self->window_id, &win_x, &win_y, &win_width, &win_height)) {
        return -1;
    }
    i32 mouse_x, mouse_y;
    u8 buttons;
    gt_mouse_query(&mouse_x, &mouse_y, &buttons);
    bool left_down = (buttons & 1) != 0;
    bool clicked_edge = left_down && !self->was_down;
    self->was_down = left_down;
    if (!clicked_edge) {
        return -1;
    }

    u32 screen_x = (u32) win_x + self->x;
    u32 screen_y = (u32) win_y + self->y;
    if ((u32) mouse_x < screen_x || (u32) mouse_x >= screen_x + self->width) {
        return -1;
    }
    int row = 0;
    while (row < self->row_count) {
        u32 row_y = screen_y + (u32) (row * (int) self->row_height);
        if ((u32) mouse_y >= row_y && (u32) mouse_y < row_y + self->row_height) {
            int old_selected = self->selected_index;
            self->selected_index = row;
            if (old_selected >= 0 && old_selected != row) {
                list_view_draw_row(self, old_selected);
            }
            list_view_draw_row(self, row);
            return row;
        }
        row = row + 1;
    }
    return -1;
}
