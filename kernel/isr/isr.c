// Called from boot/interrupts.s's isr_common_stub for every known vector.

#include "isr.h"
#include "../../drivers/io.h"
#include "../../drivers/keyboard.h"
#include "../../drivers/mouse.h"
#include "../../drivers/vbe.h"
#include "../../gfx/window.h"
#include "../../lib/strings.h"
#include "../sched/task.h"

u64 g_tick_count;

// Drives the mouse cursor's on-screen redraw (gfx/window.c's draw_cursor(),
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

        if (g_fb_enabled
            && g_tick_count - g_cursor_last_redraw_tick >= CURSOR_REDRAW_TICK_INTERVAL
            && (g_mouse_x != g_cursor_last_drawn_x || g_mouse_y != g_cursor_last_drawn_y)) {
            g_cursor_last_drawn_x = g_mouse_x;
            g_cursor_last_drawn_y = g_mouse_y;
            g_cursor_last_redraw_tick = g_tick_count;
            compositor_redraw();
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
        if (scancode < 0x80) {  // top bit set = key release, ignore those
            if (scancode == 0x0E) {  // Backspace
                if (g_line_len > 0) {
                    g_line_len = g_line_len - 1;
                    g_vga_cursor = g_vga_cursor - 1;
                    g_vga[g_vga_cursor].character = ' ';
                    g_vga[g_vga_cursor].color = 0x0F;
                    vga_update_cursor(g_vga_cursor);
                    serial_putc('\b');
                    serial_putc(' ');
                    serial_putc('\b');
                    term_scrollback_backspace();
                }
            } else {
                char c = g_scancode_table[scancode];
                if (c == '\n') {
                    g_line_buffer[g_line_len] = '\0';
                    new_line();
                    g_line_ready = true;
                } else if (c != '\0' && g_line_len < 127) {
                    g_line_buffer[g_line_len] = c;
                    g_line_len = g_line_len + 1;
                    vga_putc(c);
                    serial_putc((u8) c);
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
