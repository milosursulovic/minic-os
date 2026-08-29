#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

bool dns_query(char* hostname);
// Resolves hostname's first A record into ip_out[4].
bool dns_resolve_a(char* hostname, u8* ip_out);

#pragma GCC visibility pop
