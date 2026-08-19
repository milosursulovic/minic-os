#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

extern char g_scancode_table[128];

extern char g_line_buffer[128];
extern int g_line_len;
extern bool g_line_ready;

void init_scancode_table(void);

#pragma GCC visibility pop
