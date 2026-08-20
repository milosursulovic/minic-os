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

#pragma GCC visibility pop
