#pragma once

#include "../../types.h"

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

// Every character vga_putc() ever draws is also mirrored here (a plain
// ring buffer, wrapping on storage - g_term_write_pos itself only ever
// grows) - this is what lets a ring3 terminal window (proc/apps/terminal.c)
// show the exact same output the console shell already prints, with no
// change to how shell commands produce that output at all.
#define TERM_SCROLLBACK_SIZE 16384
extern char g_term_scrollback[TERM_SCROLLBACK_SIZE];
extern u64 g_term_write_pos;
// Backspace bypasses vga_putc() entirely (kernel/isr/isr.c's scancode
// 0x0E handler pokes g_vga[] directly, to erase in place rather than
// append) - without a separate mirror hook, the GUI terminal window
// (proc/apps/terminal.c) never learns a character was deleted and keeps
// showing it forever, even though the real VGA buffer erased it
// correctly. Appends a literal '\b' marker byte for terminal_feed() to
// interpret as "erase the previous mirrored character", same idea as a
// real terminal's backspace-is-just-another-byte-in-the-stream model.
void term_scrollback_backspace(void);

#pragma GCC visibility pop
