#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

typedef struct {
    u8 character;
    u8 color;
} vga_char;

extern volatile vga_char* g_vga;
extern int g_vga_cursor;

void outb(u16 port, u8 value);
u8 inb(u16 port);
void outw(u16 port, u16 value);
u16 inw(u16 port);
void outl(u16 port, u32 value);
u32 inl(u16 port);

void serial_putc(u8 c);
void serial_print(const char* s);
void vga_putc(char c);
void vga_print(const char* s);
// Advances both serial (real '\n') and VGA cursor to the next line.
void new_line(void);
// Enables the hardware text-mode cursor (CRTC index 0x0A/0x0B) with a
// normal underline shape - call once at boot before anything relies on
// the cursor being visible.
void vga_enable_cursor(void);
// Moves the real hardware cursor (CRTC index 0x0E/0x0F, cell index into
// the 80x25 grid) to match g_vga_cursor - called after every g_vga_cursor
// change; without this the VGA text buffer updates but the blinking
// cursor glyph itself never moves.
void vga_update_cursor(int pos);

#pragma GCC visibility pop
