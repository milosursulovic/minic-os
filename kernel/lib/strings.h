#pragma once

#include "../../types.h"

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
// Real dotted-decimal printing (0-255 per call), unlike print_hex - an
// IP address printed in hex wouldn't look like a real IP to anyone.
void print_decimal(u64 value);
// Strict "A.B.C.D" (4 decimal octets 0-255) parser - returns false for
// anything else, including a bare hostname, which is exactly how a
// caller like cmd_ping tells "was I given a literal IP or a name to
// resolve" apart.
bool parse_ip(const char* s, u8* out);

#pragma GCC visibility pop
