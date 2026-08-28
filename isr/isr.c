// Called from boot/interrupts.s's isr_common_stub for every known vector.

#include "isr.h"
#include "../drivers/io.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"
#include "../lib/strings.h"
#include "../sched/task.h"

u64 g_tick_count;

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
