#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Async ping/dns/tcp_fetch + wait (20/21 ping, 22/23 dns, 24/25 tcp_fetch).
bool syscall_net_request(u64 num, u64 a1, u64 a2, u64 a3, u64* result);

#pragma GCC visibility pop
