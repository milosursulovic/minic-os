#pragma once
#include "../core.h"
#include "../window.h"
#include "../../../kernel/gfx/font/font.h"

// TextBox - single-line, append/backspace-at-the-end only (no arrow-key
// cursor repositioning - kernel/isr/isr.c's keyboard routing for a
// focused window already ignores extended/arrow scancodes entirely; no
// selection; no multi-line, matching window_draw_text's own "single
// line only" limitation). Real, minimal first pass built directly on
// gt_focus_window/gt_read_key (syscalls 69/70) - a window must have real
// focus (via a real mouse click or by calling gt_focus_window on
// itself) for keys typed to ever reach this widget's poll() at all.
// Real, stated limitation: the underlying keystroke queue is one shared
// queue per focused WINDOW, not per-widget - two text_box widgets in
// the same window would both drain the same stream, so only one is
// meaningfully interactive per window with this design. Split out of
// the former single gui_toolkit.h.

#define TEXT_BOX_MAX_LEN 32

typedef struct {
    int window_id;
    u32 x, y, width, height;
    char text[TEXT_BOX_MAX_LEN];
    int length;
    u32 bg_color, fg_color, border_color;
} text_box;

static __attribute__((unused)) void text_box_draw(text_box* self) {
    gt_window_fill_rect_args border;
    border.id = self->window_id;
    border.x = self->x;
    border.y = self->y;
    border.width = self->width;
    border.height = self->height;
    border.color = self->border_color;
    gt_syscall(30, (u64) &border, 0, 0);

    u32 inset = self->width > 4 && self->height > 4 ? 2 : 0;
    gt_window_fill_rect_args inner;
    inner.id = self->window_id;
    inner.x = self->x + inset;
    inner.y = self->y + inset;
    inner.width = self->width - (inset * 2);
    inner.height = self->height - (inset * 2);
    inner.color = self->bg_color;
    gt_syscall(30, (u64) &inner, 0, 0);

    gt_window_draw_text_args text_args;
    text_args.id = self->window_id;
    text_args.x = self->x + inset + 2;
    text_args.y = self->y + inset + 1;
    text_args.fg_color = self->fg_color;
    text_args.bg_color = self->bg_color;
    text_args.text = self->text;
    gt_syscall(32, (u64) &text_args, 0, 0);

    // A thin cursor bar right after the last character - no font glyph
    // needed, real position always tracks self->length since there is
    // no cursor repositioning within the string.
    gt_window_fill_rect_args cursor;
    cursor.id = self->window_id;
    cursor.x = text_args.x + (u32) (self->length * (FONT_GLYPH_WIDTH + 1));
    cursor.y = self->y + inset + 1;
    cursor.width = 2;
    cursor.height = FONT_GLYPH_HEIGHT;
    cursor.color = self->fg_color;
    gt_syscall(30, (u64) &cursor, 0, 0);
}

static __attribute__((unused)) void text_box_init(text_box* self, int window_id, u32 x, u32 y, u32 width, u32 height,
                            u32 bg_color, u32 fg_color, u32 border_color, const char* initial_text) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;
    self->bg_color = bg_color;
    self->fg_color = fg_color;
    self->border_color = border_color;
    self->length = 0;
    while (initial_text[self->length] != '\0' && self->length < TEXT_BOX_MAX_LEN - 1) {
        self->text[self->length] = initial_text[self->length];
        self->length = self->length + 1;
    }
    self->text[self->length] = '\0';
    text_box_draw(self);
}

static __attribute__((unused)) bool text_box_poll(text_box* self) {
    int key = gt_read_key(self->window_id);
    if (key < 0) {
        return false;
    }
    if (key == 0x08) {  // Backspace
        if (self->length == 0) {
            return false;
        }
        self->length = self->length - 1;
        self->text[self->length] = '\0';
        text_box_draw(self);
        return true;
    }
    if (key == '\n') {  // single-line, no submit semantics yet - ignored
        return false;
    }
    if (self->length >= TEXT_BOX_MAX_LEN - 1) {
        return false;
    }
    self->text[self->length] = (char) key;
    self->length = self->length + 1;
    self->text[self->length] = '\0';
    text_box_draw(self);
    return true;
}
