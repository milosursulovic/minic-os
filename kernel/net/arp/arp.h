#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

bool ip_equals(u8* a, u8* b);
bool arp_resolve(u8* target_ip, u8* mac_out);
// Lazily brings the NIC up exactly once (e1000_init() + e1000_init_rings()).
// arp_resolve() already calls this internally for the client path; a real
// server (kernel/net/tcp/tcp.c's tcp_listen()) needs the same readiness
// before it can ever see an incoming packet, but has no IP to ARP-resolve
// first - so it calls this directly instead.
bool arp_init(void);

#pragma GCC visibility pop
