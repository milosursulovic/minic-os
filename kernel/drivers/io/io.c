// Low-level output: VGA text buffer, serial port, raw port I/O.

#include "io.h"

volatile vga_char* g_vga;
int g_vga_cursor;

char g_term_scrollback[TERM_SCROLLBACK_SIZE];
u64 g_term_write_pos;

static void term_scrollback_append(char c) {
    g_term_scrollback[g_term_write_pos % TERM_SCROLLBACK_SIZE] = c;
    g_term_write_pos = g_term_write_pos + 1;
}

void term_scrollback_backspace(void) {
    term_scrollback_append('\b');
}

void term_scrollback_clear(void) {
    term_scrollback_append('\f');
}

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
    vga_update_cursor(g_vga_cursor);
    term_scrollback_append(c);
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
    term_scrollback_append('\n');
    g_vga_cursor = ((g_vga_cursor / 80) + 1) * 80;
    if (g_vga_cursor >= 2000) {
        g_vga_cursor = 80;  // wrap; row 0 keeps the boot message (no real scrolling yet)
    }
    vga_update_cursor(g_vga_cursor);
}

void vga_enable_cursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, (u8) ((inb(0x3D5) & 0xC0) | 14));  // start scanline 14 - underline shape
    outb(0x3D4, 0x0B);
    outb(0x3D5, (u8) ((inb(0x3D5) & 0xE0) | 15));  // end scanline 15
}

void vga_update_cursor(int pos) {
    outb(0x3D4, 0x0F);
    outb(0x3D5, (u8) (pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (u8) ((pos >> 8) & 0xFF));
}
