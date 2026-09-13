#pragma once

#include "../../../types.h"

// Neighbor Discovery Protocol (RFC 4861) - IPv6's ARP-equivalent (Neighbor
// Solicitation/Advertisement) plus Router Solicitation/Advertisement for
// SLAAC (RFC 4862). Same "client only, no responder" scope
// kernel/net/arp/arp.c already established for ARP - this kernel has no
// IPv6 "server"/router role anywhere else either.

#pragma GCC visibility push(hidden)

// Resolves target_ip6's MAC via a real Neighbor Solicitation/Advertisement
// exchange (cached afterward, same fixed-size-cache shape as
// kernel/net/arp/arp.c's arp_resolve()). Brings the NIC up itself if
// needed (kernel/net/arp/arp.c's own arp_init(), reused as-is).
bool ndp_resolve(u8* target_ip6, u8* mac_out);

// Best-effort, one-shot SLAAC (RFC 4862): sends a Router Solicitation,
// waits (tick-bounded) for a real Router Advertisement, and if one
// arrives, derives kernel/net/ipv6/ipv6.h's g_my_ipv6_global from the
// RA's advertised prefix + our own EUI-64 interface ID. Not fatal if no
// RA ever arrives - kernel/net/ip/ip.c's ensure_ip_configured() DHCP
// fallback precedent - link-local addressing keeps working regardless.
// Returns true only if a real global address was obtained.
bool ndp_do_slaac(void);

#pragma GCC visibility pop
