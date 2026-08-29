#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

bool streq(const char* a, const char* b);
int strlen_(const char* s);
bool starts_with(const char* s, const char* prefix);
u64 parse_hex(const char* s);
void print_hex(u64 value);
int format_hex(u64 value, u8* out);
// base + "/" + name, or just name if base is empty ("" = root, same
// convention proc/apps/file_manager.c's own current_path already uses).
// No bounds checking - callers own a big-enough out buffer, same as
// every other fixed-buffer helper in this codebase.
void join_path(char* out, const char* base, const char* name);

#pragma GCC visibility pop
