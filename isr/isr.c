// Called from boot/interrupts.s's isr_common_stub for every vector it
// knows about. An ordinary function, called with an ordinary `call` -
// nothing interrupt-specific about its body.

#include "isr.h"
#include "../drivers/io.h"
#include "../drivers/keyboard.h"
#include "../lib/strings.h"

u64 g_tick_count;

u64 read_cr2(void) {
    u64 value;
    __asm__ volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

void interrupt_handler(u64 vector, u64 error_code) {
    if (vector == 32) {
        g_tick_count = g_tick_count + 1;
        if (g_tick_count % 100 == 0) {
            serial_putc('.');  // one dot per ~1 second at 100Hz, proves the timer keeps firing
        }
        outb(0x20, 0x20);  // EOI
        return;
    }

    if (vector == 33) {
        u8 scancode = inb(0x60);
        if (scancode < 0x80) {  // top bit set = key release, ignore those
            char c = g_scancode_table[scancode];
            if (c == '\n') {
                g_line_buffer[g_line_len] = '\0';
                g_line_ready = true;
            } else if (c != '\0' && g_line_len < 127) {
                g_line_buffer[g_line_len] = c;
                g_line_len = g_line_len + 1;
                vga_putc(c);
                serial_putc((u8) c);
            }
        }
        outb(0x20, 0x20);
        return;
    }

    if (vector == 0) {
        serial_print("divide by zero, halting\n");
    } else if (vector == 13) {
        serial_print("general protection fault, error_code=0x");
        print_hex(error_code);
        serial_print(", halting\n");
    } else if (vector == 14) {
        u64 fault_addr = read_cr2();
        serial_print("page fault at 0x");
        print_hex(fault_addr);
        // bit 4 (0x10) is set specifically for an instruction-fetch
        // violation (an NX check failing), distinct from bit 1 (write)
        // or a not-present fault.
        serial_print(", error_code=0x");
        print_hex(error_code);
        serial_print(", halting\n");
    } else {
        serial_print("unhandled exception, halting\n");
    }
    for (;;) {
        __asm__ volatile("hlt");
    }
}
