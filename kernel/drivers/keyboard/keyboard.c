// Scancode set 1 table (lowercase only, no shift/modifiers) and the line buffer
// the IRQ1 handler (isr.c) fills a character at a time.

#include "keyboard.h"

char g_scancode_table[128];
char g_scancode_table_shifted[128];

char g_line_buffer[128];
int g_line_len;
bool g_line_ready;
int g_line_cursor;

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

    g_scancode_table_shifted[0x1E] = 'A'; g_scancode_table_shifted[0x30] = 'B'; g_scancode_table_shifted[0x2E] = 'C';
    g_scancode_table_shifted[0x20] = 'D'; g_scancode_table_shifted[0x12] = 'E'; g_scancode_table_shifted[0x21] = 'F';
    g_scancode_table_shifted[0x22] = 'G'; g_scancode_table_shifted[0x23] = 'H'; g_scancode_table_shifted[0x17] = 'I';
    g_scancode_table_shifted[0x24] = 'J'; g_scancode_table_shifted[0x25] = 'K'; g_scancode_table_shifted[0x26] = 'L';
    g_scancode_table_shifted[0x32] = 'M'; g_scancode_table_shifted[0x31] = 'N'; g_scancode_table_shifted[0x18] = 'O';
    g_scancode_table_shifted[0x19] = 'P'; g_scancode_table_shifted[0x10] = 'Q'; g_scancode_table_shifted[0x13] = 'R';
    g_scancode_table_shifted[0x1F] = 'S'; g_scancode_table_shifted[0x14] = 'T'; g_scancode_table_shifted[0x16] = 'U';
    g_scancode_table_shifted[0x2F] = 'V'; g_scancode_table_shifted[0x11] = 'W'; g_scancode_table_shifted[0x2D] = 'X';
    g_scancode_table_shifted[0x15] = 'Y'; g_scancode_table_shifted[0x2C] = 'Z';
    g_scancode_table_shifted[0x39] = ' ';
    g_scancode_table_shifted[0x1C] = '\n';

    g_scancode_table_shifted[0x02] = '!'; g_scancode_table_shifted[0x03] = '@'; g_scancode_table_shifted[0x04] = '#';
    g_scancode_table_shifted[0x05] = '$'; g_scancode_table_shifted[0x06] = '%'; g_scancode_table_shifted[0x07] = '^';
    g_scancode_table_shifted[0x08] = '&'; g_scancode_table_shifted[0x09] = '*'; g_scancode_table_shifted[0x0A] = '(';
    g_scancode_table_shifted[0x0B] = ')';

    g_scancode_table_shifted[0x34] = '>';
    g_scancode_table_shifted[0x35] = '?';

    // The colon-hex/8.3-uppercase real pain points this fixes (a previous
    // session found and worked around both): 0x27 is the physical
    // semicolon/colon key (unshifted ';', shifted ':') - not previously
    // in g_scancode_table at all, so add both here.
    g_scancode_table[0x27] = ';';
    g_scancode_table_shifted[0x27] = ':';
}
