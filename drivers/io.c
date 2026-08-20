// Low-level output: the VGA text buffer, the serial port, and raw port
// I/O (`in`/`out`). Real GCC inline asm has real operand binding, so
// unlike the old MiniC version (which had to stage every value through a
// module-level global before an `asm("...")` block with no operand
// binding could reach it - see the removed g_out_port/g_out_byte-style
// relay globals in the kernel's MiniC-era history) this is direct: the
// value goes straight into the instruction via a real constraint.

#include "io.h"

volatile vga_char* g_vga;
int g_vga_cursor;

void outb(u16 port, u8 value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

u8 inb(u16 port) {
    u8 value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void outw(u16 port, u16 value) {
    __asm__ volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

u16 inw(u16 port) {
    u16 value;
    __asm__ volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void outl(u16 port, u32 value) {
    __asm__ volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

u32 inl(u16 port) {
    u32 value;
    __asm__ volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void serial_putc(u8 c) {
    outb(0x3F8, c);
}

void serial_print(const char* s) {
    int i = 0;
    while (s[i] != '\0') {
        serial_putc((u8) s[i]);
        i = i + 1;
    }
}

void vga_putc(char c) {
    g_vga[g_vga_cursor].character = (u8) c;
    g_vga[g_vga_cursor].color = 0x0F;
    g_vga_cursor = g_vga_cursor + 1;
}

void vga_print(const char* s) {
    int i = 0;
    while (s[i] != '\0') {
        vga_putc(s[i]);
        i = i + 1;
    }
}

void new_line(void) {
    serial_putc('\n');
    g_vga_cursor = ((g_vga_cursor / 80) + 1) * 80;
    if (g_vga_cursor >= 2000) {
        g_vga_cursor = 80;  // wrap - no real scrolling yet, row 0 stays the boot message
    }
}
