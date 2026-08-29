// Scancode set 1 table (lowercase only, no shift/modifiers) and the line buffer
// the IRQ1 handler (isr.c) fills a character at a time.

#include "keyboard.h"

char g_scancode_table[128];

char g_line_buffer[128];
int g_line_len;
bool g_line_ready;

void init_scancode_table(void) {
    g_scancode_table[0x1E] = 'a'; g_scancode_table[0x30] = 'b'; g_scancode_table[0x2E] = 'c';
    g_scancode_table[0x20] = 'd'; g_scancode_table[0x12] = 'e'; g_scancode_table[0x21] = 'f';
    g_scancode_table[0x22] = 'g'; g_scancode_table[0x23] = 'h'; g_scancode_table[0x17] = 'i';
    g_scancode_table[0x24] = 'j'; g_scancode_table[0x25] = 'k'; g_scancode_table[0x26] = 'l';
    g_scancode_table[0x32] = 'm'; g_scancode_table[0x31] = 'n'; g_scancode_table[0x18] = 'o';
    g_scancode_table[0x19] = 'p'; g_scancode_table[0x10] = 'q'; g_scancode_table[0x13] = 'r';
    g_scancode_table[0x1F] = 's'; g_scancode_table[0x14] = 't'; g_scancode_table[0x16] = 'u';
    g_scancode_table[0x2F] = 'v'; g_scancode_table[0x11] = 'w'; g_scancode_table[0x2D] = 'x';
    g_scancode_table[0x15] = 'y'; g_scancode_table[0x2C] = 'z';
    g_scancode_table[0x39] = ' ';
    g_scancode_table[0x1C] = '\n';  // enter -> newline

    g_scancode_table[0x02] = '1'; g_scancode_table[0x03] = '2'; g_scancode_table[0x04] = '3';
    g_scancode_table[0x05] = '4'; g_scancode_table[0x06] = '5'; g_scancode_table[0x07] = '6';
    g_scancode_table[0x08] = '7'; g_scancode_table[0x09] = '8'; g_scancode_table[0x0A] = '9';
    g_scancode_table[0x0B] = '0';

    g_scancode_table[0x34] = '.';
    g_scancode_table[0x35] = '/';
}
