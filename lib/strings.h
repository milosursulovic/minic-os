#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

bool streq(const char* a, const char* b);
int strlen_(const char* s);
bool starts_with(const char* s, const char* prefix);
u64 parse_hex(const char* s);
void print_hex(u64 value);
int format_hex(u64 value, u8* out);

#pragma GCC visibility pop
