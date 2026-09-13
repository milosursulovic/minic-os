#pragma once

#include "../../../types.h"

// Hand-written DHCP client (RFC 2131) - see dhcp.c's own top comment for
// scope (DISCOVER/OFFER/REQUEST/ACK only, one lease per boot, no
// renewal/T1/T2 timers).

#pragma GCC visibility push(hidden)

// Runs one full DORA exchange. On success, writes the assigned address
// and any offered subnet mask/router/DNS server straight into
// kernel/net/ip/ip.h's g_my_ip/g_subnet_mask/g_gateway_ip/g_dns_server_ip
// and returns true. Returns false (leaving those globals untouched) if
// no NIC is present, no server ever replies, or the exchange times out -
// kernel/net/ip/ip.c's ensure_ip_configured() is the only real caller,
// falling back to ip_init()'s hardcoded defaults on false.
bool dhcp_client(void);

#pragma GCC visibility pop
