// The terminal emulator (Faza II point 23) - a real GUI window mirroring
// the kernel console shell's actual output. Auto-spawned by kmain.c at
// boot (Mechanism A, same as desktop_shell.c/init.c/hello_service.c).
//
// Deliberately NOT a second keyboard-input target: the physical keyboard
// still only ever fills g_line_buffer (kernel/isr/isr.c, unchanged) - this
// window just polls kernel/drivers/io.c's g_term_scrollback (syscall 36,
// gt_term_read) and redraws whatever the console shell already printed.
// That's what lets this share every one of run_command()'s ~70 commands
// with zero shell.c refactor, and zero risk to the console's own
// existing keyboard-driven behavior (real regression risk otherwise -
// see project memory on this session's QEMU sendkey test workflow).
//
// Known limitation: the font (kernel/gfx/font.h) only has A-Z, 0-9, space, and
// `. , ! ? : -` - real shell output uses lowercase and other punctuation
// (`=`, `/`, `<`, `>`, ...) constantly, so mirrored text will show gaps
// wherever an unsupported character would be. Not fixed here - expanding
// the font is its own future milestone.
//
// _start must be at offset 0 - see ring3prog.c's own comment on this;
// same __attribute__((section(".text.start"))) + ring3.ld requirement.

#include "../../../types.h"
#include "../../gui_toolkit.h"
#include "../../../kernel/gfx/font/font.h"  // FONT_GLYPH_WIDTH/HEIGHT only - font_get_glyph() itself is never called from ring3

#define WINDOW_X 100
#define WINDOW_Y 60
#define WINDOW_WIDTH 500
#define WINDOW_HEIGHT 320
#define TITLEBAR_HEIGHT 20
#define BODY_WIDTH WINDOW_WIDTH
#define BODY_HEIGHT (WINDOW_HEIGHT - TITLEBAR_HEIGHT)

#define CHAR_W (FONT_GLYPH_WIDTH + 1)
#define CHAR_H (FONT_GLYPH_HEIGHT + 1)
#define COLS (BODY_WIDTH / CHAR_W)
#define ROWS (BODY_HEIGHT / CHAR_H)

#define BODY_COLOR 0x00000000u
#define TITLE_COLOR 0x00303030u
#define TEXT_COLOR 0x00FFFFFFu

#define READ_CHUNK 512

static char g_lines[ROWS][COLS + 1];
static int g_cur_row;
static int g_cur_col;
static bool g_scrolled_this_batch;

static void terminal_scroll_up(void) {
    int row = 0;
    while (row < ROWS - 1) {
        int col = 0;
        while (col <= COLS) {
            g_lines[row][col] = g_lines[row + 1][col];
            col = col + 1;
        }
        row = row + 1;
    }
    g_lines[ROWS - 1][0] = '\0';
    g_scrolled_this_batch = true;
}

static void terminal_feed(char c) {
    if (c == '\n') {
        g_lines[g_cur_row][g_cur_col] = '\0';
        g_cur_row = g_cur_row + 1;
        g_cur_col = 0;
        if (g_cur_row >= ROWS) {
            terminal_scroll_up();
            g_cur_row = ROWS - 1;
        }
        return;
    }
    if (c == '\b') {
        // The console's own backspace handling (isr.c) never crosses a
        // line boundary either (same-line-only erase) - mirror that
        // exactly, don't invent a "back onto the previous row" behavior
        // the real console doesn't have.
        if (g_cur_col > 0) {
            g_cur_col = g_cur_col - 1;
            g_lines[g_cur_row][g_cur_col] = '\0';
        }
        return;
    }
    if (g_cur_col < COLS) {
        // Display-only uppercasing - the font (kernel/gfx/font.h) has no
        // lowercase glyphs at all, so real shell output (almost all
        // lowercase) would otherwise render as mostly blank cells. This
        // doesn't touch the real shell/g_line_buffer/g_term_scrollback
        // in any way, only what THIS window renders from its own local
        // copy - so it's honest about being a display transform, not a
        // claim that the console shell is actually uppercase.
        if (c >= 'a' && c <= 'z') {
            c = (char) (c - 'a' + 'A');
        }
        g_lines[g_cur_row][g_cur_col] = c;
        g_cur_col = g_cur_col + 1;
        g_lines[g_cur_row][g_cur_col] = '\0';
    }
    // Longer lines are truncated, not wrapped - a known, simple limit.
}

// Redraws exactly one row: a bg-colored fill of its full cell width (so a
// shorter new line doesn't leave stale pixels from a longer old one at
// that row - window_draw_text() only paints as far as its string's own
// length) then the row's text, if any.
static void terminal_redraw_row(int window_id, int row) {
    gt_window_fill_rect_args bg_args;
    bg_args.id = window_id;
    bg_args.x = 0;
    bg_args.y = (u32) (row * CHAR_H);
    bg_args.width = BODY_WIDTH;
    bg_args.height = CHAR_H;
    bg_args.color = BODY_COLOR;
    gt_syscall(30, (u64) &bg_args, 0, 0);

    if (g_lines[row][0] != '\0') {
        gt_window_draw_text_args text_args;
        text_args.id = window_id;
        text_args.x = 0;
        text_args.y = (u32) (row * CHAR_H);
        text_args.fg_color = TEXT_COLOR;
        text_args.bg_color = BODY_COLOR;
        text_args.text = &g_lines[row][0];
        gt_syscall(32, (u64) &text_args, 0, 0);
    }
}

__attribute__((section(".text.start")))
void _start(void) {
    int window_id = gt_window_create(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT,
                                      BODY_COLOR, TITLE_COLOR);

    u64 last_pos = 0;
    char buf[READ_CHUNK];

    for (;;) {
        u32 copied = gt_term_read(last_pos, &buf[0], READ_CHUNK);
        if (copied > 0) {
            // Redrawing every row on every update - up to ROWS
            // window_draw_text calls, each triggering its own full
            // compositor_redraw() (an 800x600 back-buffer blit) - was
            // real enough overhead, called from an unthrottled poll
            // loop, that it looked like the terminal window had frozen
            // entirely in real interactive use. A batch of newly-fed
            // characters almost always only touches the row(s) between
            // where this batch started and the current row - redraw
            // just that range, and only fall back to a full repaint if
            // a scroll actually shifted every row's content.
            int row_before_batch = g_cur_row;
            g_scrolled_this_batch = false;

            u32 i = 0;
            while (i < copied) {
                terminal_feed(buf[i]);
                i = i + 1;
            }
            last_pos = last_pos + copied;

            if (g_scrolled_this_batch) {
                int row = 0;
                while (row < ROWS) {
                    terminal_redraw_row(window_id, row);
                    row = row + 1;
                }
            } else {
                int row = row_before_batch;
                while (row <= g_cur_row) {
                    terminal_redraw_row(window_id, row);
                    row = row + 1;
                }
            }
        }
    }
}
