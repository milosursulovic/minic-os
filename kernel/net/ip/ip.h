#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

extern u8 g_my_ip[4];
// QEMU SLIRP's fixed default gateway/DNS-relay addresses - one real
// source of truth (ip_init() sets all three together), instead of the
// same two values re-hardcoded separately in shell.c and net/tcp/tcp.c.
extern u8 g_gateway_ip[4];
extern u8 g_dns_server_ip[4];
// Faza I point 10, networking-completion arc item 2 (DHCP): stored for
// completeness once a real lease is obtained - no routing logic consumes
// this yet (tcp.c/icmp.c still always route via g_gateway_ip, udp.c still
// always ARPs its target directly - a real on-link/off-link routing
// table is a separate, future concern, not part of this item).
extern u8 g_subnet_mask[4];

// Hardcoded QEMU SLIRP defaults - kept as ensure_ip_configured()'s
// fallback if DHCP genuinely fails (no NIC, no server, timeout), and
// callable directly by anything that wants the old always-static
// behavior (nothing does anymore - every real call site below now goes
// through ensure_ip_configured() instead).
void ip_init(void);
// Faza I point 10, networking-completion arc item 2: runs a real DHCP
// DISCOVER/OFFER/REQUEST/ACK exchange (kernel/net/dhcp/dhcp.h) exactly
// once (a done-latch, same shape as kernel/net/arp/arp.c's own
// g_arp_nic_ready), falling back to ip_init()'s hardcoded defaults if it
// fails. Deliberately lazy, not called eagerly at boot - matches this
// kernel's existing "network stack only comes up on first real use"
// philosophy (arp_init()'s own lazy NIC bring-up), so a boot that never
// touches networking pays zero DHCP latency. This is what every former
// ip_init() call site (kernel/net/udp/udp.c, kernel/net/arp/arp.c,
// kernel/net/tcp/tcp.c, kernel/net/icmp/icmp.c, shell/shell/commands/
// net.c) now calls instead.
void ensure_ip_configured(void);
u16 ip_checksum(u8* data, u32 len);
void ip_build_header(u8* out, u8* src_ip, u8* dst_ip, u8 protocol, u16 payload_len);

#pragma GCC visibility pop
