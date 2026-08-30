// Called from boot/interrupts.s's isr_common_stub for every known vector.

#include "isr.h"
#include "../drivers/io/io.h"
#include "../drivers/keyboard/keyboard.h"
#include "../drivers/mouse/mouse.h"
#include "../drivers/vbe/vbe.h"
#include "../gfx/window/window.h"
#include "../lib/strings.h"
#include "../sched/task.h"
#include "../../shell/editor/editor.h"
#include "../../shell/shell/shell.h"

u64 g_tick_count;

// Scancode set 1 sends an extended key (arrows, etc) as two separate IRQ1
// bytes: a 0xE0 prefix, then the actual code. Set on the prefix byte,
// consumed (and cleared) on the very next IRQ1 - real key events always
// arrive as consecutive interrupts, nothing else runs on this task in
// between (task 0's own scheduling invariant, see CLAUDE.md).
static bool g_extended_prefix;

// Drives the mouse cursor's on-screen redraw (kernel/gfx/window.c's draw_cursor(),
// composited last in every compositor_redraw() call) independent of
// whatever else is or isn't changing on screen - without this, the cursor
// only moves when some unrelated window redraw happens to fire (a button
// state change, the uptime label's tick, etc), which looks frozen/laggy
// during a plain mouse move with no clicks. Same tick-delta-AND-real-change
// double guard as desktop_shell.c's uptime label and terminal.c's row
// redraw - a compositor_redraw() is a full 800x600 backbuffer pass, so
// firing it on every single timer tick (QEMU/TCG's PIT runs far faster
// than the nominal 100Hz - see CLAUDE.md) would reintroduce the same
// redraw-storm bug class already hit twice this session.
#define CURSOR_REDRAW_TICK_INTERVAL 3
static i32 g_cursor_last_drawn_x = -1;
static i32 g_cursor_last_drawn_y = -1;
static u64 g_cursor_last_redraw_tick;

u64 read_cr2(void) {
    u64 value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

void interrupt_handler(u64 vector, u64 error_code, u64 saved_rip) {
    if (vector == 32) {
        g_tick_count = g_tick_count + 1;
        if (g_tick_count % 100 == 0) {
            serial_putc('.');  // one dot per ~1s at 100Hz, proves the timer keeps firing
        }

        if (g_fb_enabled) {
            // compositor_handle_mouse() tracks button-press edges itself,
            // so it must run every tick regardless of the redraw
            // throttle below - only the resulting compositor_redraw()
            // (an 800x600 backbuffer pass) is throttled, same reasoning
            // as the plain cursor-move check it's now combined with:
            // firing a full redraw on every single timer tick during an
            // active drag would be the same redraw-storm bug class
            // already fixed twice elsewhere this session.
            bool wm_changed = compositor_handle_mouse();
            bool cursor_moved = (g_mouse_x != g_cursor_last_drawn_x || g_mouse_y != g_cursor_last_drawn_y);
            if (g_tick_count - g_cursor_last_redraw_tick >= CURSOR_REDRAW_TICK_INTERVAL
                && (cursor_moved || wm_changed)) {
                g_cursor_last_drawn_x = g_mouse_x;
                g_cursor_last_drawn_y = g_mouse_y;
                g_cursor_last_redraw_tick = g_tick_count;
                compositor_redraw();
            }
        }

        outb(0x20, 0x20);  // EOI before yield() might switch away

        // Calling yield() from inside the timer ISR is what makes scheduling
        // preemptive: this call is just suspended mid-call like a voluntary
        // yield(), and the trap frame rides along for a correct iretq later.
        yield();
        return;
    }

    if (vector == 33) {
        u8 scancode = inb(0x60);

        if (scancode == 0xE0) {  // extended-key prefix - the real code follows on the next IRQ1
            g_extended_prefix = true;
            outb(0x20, 0x20);
            return;
        }
        bool extended = g_extended_prefix;
        g_extended_prefix = false;

        // Real keyboard-to-window routing (Faza II point 18) - a GUI
        // window has focus, so real keystrokes go to its own key queue
        // instead of the console shell/editor below, which stays 100%
        // unchanged when nothing is focused (g_focused_window_id == -1,
        // the default, and everything this project tested before this
        // existed). Deliberately minimal: extended keys (arrows/Delete)
        // are ignored here - no GUI widget needs them yet - and only a
        // real key-press scancode (top bit clear) is translated, same
        // g_scancode_table every console keystroke already uses, plus a
        // real Backspace (0x0E has no g_scancode_table entry - the
        // console handles it via its own dedicated branch below, so a
        // focused window needs the same explicit case).
        if (g_focused_window_id >= 0) {
            if (!extended && scancode < 0x80) {
                if (scancode == 0x0E) {
                    window_push_key((char) 0x08);
                } else {
                    char c = g_scancode_table[scancode];
                    if (c != '\0') {
                        window_push_key(c);
                    }
                }
            }
            outb(0x20, 0x20);
            return;
        }

        if (extended) {
            if (!g_editor_active) {  // no arrow-key history recall/cursor movement inside a full-screen edit session
                if (scancode == 0x48) {  // Up arrow (press - its release is 0xE0 0xC8, ignored below)
                    shell_history_up();
                } else if (scancode == 0x50) {  // Down arrow
                    shell_history_down();
                } else if (scancode == 0x4B) {  // Left arrow
                    if (g_line_cursor > 0) {
                        g_line_cursor = g_line_cursor - 1;
                        g_vga_cursor = g_vga_cursor - 1;
                        vga_update_cursor(g_vga_cursor);
                        term_scrollback_cursor_left();
                    }
                } else if (scancode == 0x4D) {  // Right arrow
                    if (g_line_cursor < g_line_len) {
                        g_line_cursor = g_line_cursor + 1;
                        g_vga_cursor = g_vga_cursor + 1;
                        vga_update_cursor(g_vga_cursor);
                        term_scrollback_cursor_right();
                    }
                } else if (scancode == 0x53) {  // Delete (the dedicated key, not the numpad one)
                    if (g_line_cursor < g_line_len) {
                        int i = g_line_cursor;
                        while (i < g_line_len - 1) {
                            g_line_buffer[i] = g_line_buffer[i + 1];
                            i = i + 1;
                        }
                        g_line_len = g_line_len - 1;

                        int tail_len = g_line_len - g_line_cursor;
                        int j = g_line_cursor;
                        while (j < g_line_len) {
                            vga_putc(g_line_buffer[j]);
                            serial_putc((u8) g_line_buffer[j]);
                            j = j + 1;
                        }
                        // The vacated trailing cell must go through vga_putc
                        // too (not a direct g_vga[] write) - it needs the
                        // exact same automatic scrollback mirroring every
                        // other character here gets, or the GUI terminal's
                        // own tracked cursor column would fall one short of
                        // where the "move cursor back" loop below assumes
                        // it landed.
                        vga_putc(' ');
                        serial_putc(' ');

                        int k = 0;
                        while (k < tail_len + 1) {
                            g_vga_cursor = g_vga_cursor - 1;
                            term_scrollback_cursor_left();
                            k = k + 1;
                        }
                        vga_update_cursor(g_vga_cursor);
                    }
                }
            }
            outb(0x20, 0x20);
            return;
        }

        if (scancode < 0x80) {  // top bit set = key release, ignore those
            if (g_editor_active) {  // shell/editor.c owns every keystroke while a full-screen edit session is open
                editor_handle_scancode(scancode);
            } else if (scancode == 0x0E) {  // Backspace - removes the char BEFORE the cursor, not always the last one
                if (g_line_cursor > 0) {
                    int i = g_line_cursor - 1;
                    while (i < g_line_len - 1) {
                        g_line_buffer[i] = g_line_buffer[i + 1];
                        i = i + 1;
                    }
                    g_line_len = g_line_len - 1;
                    g_line_cursor = g_line_cursor - 1;
                    g_vga_cursor = g_vga_cursor - 1;
                    // This initial one-column step (real cursor moving back
                    // to the deletion point, before anything's redrawn)
                    // needs its own mirror marker too - without it the
                    // mirror's net movement across this whole Backspace
                    // would come out zero (it gets this many chars forward
                    // from the redraw below, then the same number back at
                    // the end - this one extra step is what actually moves
                    // it left by one overall, matching the real cursor).
                    term_scrollback_cursor_left();

                    int tail_len = g_line_len - g_line_cursor;
                    int j = g_line_cursor;
                    while (j < g_line_len) {
                        vga_putc(g_line_buffer[j]);
                        serial_putc((u8) g_line_buffer[j]);
                        j = j + 1;
                    }
                    // The vacated trailing cell must go through vga_putc too
                    // (not a direct g_vga[] write) - same reasoning as the
                    // Delete-key branch above: it needs the exact same
                    // automatic scrollback mirroring every other character
                    // here gets.
                    vga_putc(' ');
                    serial_putc(' ');

                    int k = 0;
                    while (k < tail_len + 1) {
                        g_vga_cursor = g_vga_cursor - 1;
                        term_scrollback_cursor_left();
                        k = k + 1;
                    }
                    vga_update_cursor(g_vga_cursor);
                }
            } else if (scancode == 0x0F) {  // Tab - shell/shell.c owns command/argument completion
                shell_tab_complete();
            } else {
                char c = g_scancode_table[scancode];
                if (c == '\n') {
                    // A trailing space (e.g. Tab-completing a no-argument
                    // command, which appends one ready for a next argument
                    // that never comes) would otherwise break every
                    // exact-match dispatch check in shell.c (streq(...,
                    // "help") doesn't match "help ") - trim it here, same
                    // as any real shell silently ignoring trailing
                    // whitespace on a submitted line.
                    while (g_line_len > 0 && g_line_buffer[g_line_len - 1] == ' ') {
                        g_line_len = g_line_len - 1;
                    }
                    g_line_buffer[g_line_len] = '\0';
                    new_line();
                    shell_history_add(g_line_buffer);
                    g_line_ready = true;
                } else if (c != '\0' && g_line_len < 127) {
                    // Inserts AT the cursor (shifts the tail right first) -
                    // this is a strict superset of plain append: when the
                    // cursor is already at the end (the common case),
                    // tail_len below is 0 and the "move cursor back" loop
                    // never runs, so this behaves exactly like the old
                    // append-only code.
                    int i = g_line_len;
                    while (i > g_line_cursor) {
                        g_line_buffer[i] = g_line_buffer[i - 1];
                        i = i - 1;
                    }
                    g_line_buffer[g_line_cursor] = c;
                    g_line_len = g_line_len + 1;

                    int j = g_line_cursor;
                    while (j < g_line_len) {
                        vga_putc(g_line_buffer[j]);
                        serial_putc((u8) g_line_buffer[j]);
                        j = j + 1;
                    }
                    g_line_cursor = g_line_cursor + 1;

                    int tail_len = g_line_len - g_line_cursor;
                    int k = 0;
                    while (k < tail_len) {
                        g_vga_cursor = g_vga_cursor - 1;
                        term_scrollback_cursor_left();
                        k = k + 1;
                    }
                    vga_update_cursor(g_vga_cursor);
                }
            }
        }
        outb(0x20, 0x20);
        return;
    }

    if (vector == 44) {
        mouse_handle_byte(inb(0x60));
        outb(0xA0, 0x20);  // EOI to the slave PIC first - it's the one that actually saw this IRQ
        outb(0x20, 0x20);  // then the master, since IRQ12 reached it only via the cascade line
        return;
    }

    if (vector == 0) {
        serial_print("divide by zero, halting\n");
    } else if (vector == 13) {
        serial_print("general protection fault, error_code=0x");
        print_hex(error_code);
        serial_print(", rip=0x");
        print_hex(saved_rip);
        serial_print(", halting\n");
    } else if (vector == 14) {
        u64 fault_addr = read_cr2();
        serial_print("page fault at 0x");
        print_hex(fault_addr);
        serial_print(", error_code=0x");
        print_hex(error_code);
        serial_print(", rip=0x");
        print_hex(saved_rip);
        serial_print(", halting\n");
    } else {
        serial_print("unhandled exception, halting\n");
    }
    for (;;) {
        __asm__ volatile("hlt");
    }
}
