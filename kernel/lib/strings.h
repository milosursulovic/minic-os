#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

bool streq(const char* a, const char* b);
int strlen_(const char* s);
bool starts_with(const char* s, const char* prefix);
u64 parse_hex(const char* s);
void print_hex(u64 value);
int format_hex(u64 value, u8* out);
// Like format_hex but base-10, for callers that need decimal text (e.g. a
// real Content-Length header value), not output straight to the console.
int format_decimal(u64 value, u8* out);
// base + "/" + name - always emits a leading "/" (base=="" produces
// "/name", entering a mount from the VFS virtual root), same convention
// proc/apps/file_manager.c's own local join_path already uses. No bounds
// checking - callers own a big-enough out buffer, same as every other
// fixed-buffer helper in this codebase.
void join_path(char* out, const char* base, const char* name);
// Real dotted-decimal printing (0-255 per call), unlike print_hex - an
// IP address printed in hex wouldn't look like a real IP to anyone.
void print_decimal(u64 value);
// Strict "A.B.C.D" (4 decimal octets 0-255) parser - returns false for
// anything else, including a bare hostname, which is exactly how a
// caller like cmd_ping tells "was I given a literal IP or a name to
// resolve" apart.
bool parse_ip(const char* s, u8* out);
// Parses a colon-hex IPv6 literal (RFC 5952 textual form, including one
// "::" zero-run compression) into out[0..15]. Faza I point 10,
// networking-completion arc item 3.
bool parse_ip6(const char* s, u8* out);
// Plain base-10 unsigned parser (e.g. a port number typed at the shell) -
// non-digit characters end the parse, same "stop at the first thing that
// doesn't fit" tolerance as parse_hex.
u32 parse_decimal_u32(const char* s);
// Returns the index of the first occurrence of `needle` within
// `haystack[0..haystack_len)`, or -1 if not found. Byte-exact substring
// search (not text-encoding aware) - needed by kernel/net/http/http.c to
// find "\r\n\r\n"/": " within a raw, not-yet-null-terminated response
// buffer, where `streq`/`starts_with`'s null-terminated-string assumption
// doesn't apply.
int find_bytes(const u8* haystack, int haystack_len, const char* needle);
// Case-insensitive comparison of the first `len` bytes of `a` against the
// (null-terminated) string `b` - real HTTP header names are
// case-insensitive by spec (RFC 7230 3.2), e.g. "Content-Length" vs
// "content-length".
bool streq_ci_n(const char* a, const char* b, int len);

#pragma GCC visibility pop
