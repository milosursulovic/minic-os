#pragma once

#include "../types.h"

// Every declaration below gets hidden visibility (matches -fvisibility=
// hidden in the Makefile's CFLAGS) - without this, a mere `extern`
// declaration of a symbol this TU doesn't define compiles to a
// GOT/PLT-indirected reference (`sym@GOTPCREL(%rip)`/`sym@PLT`), which
// `as --32` can't assemble at all (ELF64-only relocation syntax). Hidden
// visibility is correct here regardless: this is one statically-linked
// kernel image, never dynamically linked, so no symbol is ever
// interposed. Every header in this kernel follows this same
// push/pop-around-the-whole-file pattern.
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

#pragma GCC visibility pop
