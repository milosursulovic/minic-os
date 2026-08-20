#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

bool dns_query(char* hostname);
// Resolves hostname's first A record into ip_out[4]. Real answer-section
// parsing (handles both a compressed name pointer and a literal one),
// unlike dns_query() which only checks the header - used by net/tcp.c so
// a TCP demo target doesn't have to be a hardcoded, driftable IP.
bool dns_resolve_a(char* hostname, u8* ip_out);

#pragma GCC visibility pop
