#pragma once

#include "../../../types.h"

// IPv6 (Faza I point 10, networking-completion arc item 3) - header
// construction, the IPv6 pseudo-header checksum (RFC 8200 8.1, mandatory
// for ICMPv6 unlike IPv4's optional ICMP checksum - a real, different
// pseudo-header from kernel/net/ip/ip.c's IPv4 one, not reusable as-is),
// and address derivation. No fragmentation, no extension headers, no
// options - same "no options" simplicity kernel/net/ip/ip.c's own IPv4
// header already has.

#pragma GCC visibility push(hidden)

// fe80::/64 + a modified-EUI-64 interface ID derived from the NIC's own
// MAC (RFC 4291 appendix A) - computed once, deterministically, the
// instant a NIC exists. No network round-trip needed, always valid.
extern u8 g_my_ipv6_link_local[16];

// Filled in by a real SLAAC exchange (kernel/net/ndp/ndp.h's
// ndp_do_slaac()) once a genuine Router Advertisement is processed - a
// best-effort, one-shot attempt (kernel/net/ip/ip.c's
// ensure_ip_configured() precedent: not fatal if it never arrives).
extern u8 g_my_ipv6_global[16];
extern bool g_ipv6_global_valid;
extern u8 g_ipv6_default_router[16];  // the RA source's link-local address

void ipv6_init_link_local(void);

// RFC 4862 SLAAC: combines an advertised prefix (from a real Router
// Advertisement's Prefix Information option) with our own EUI-64
// interface ID - the same derivation ipv6_init_link_local() uses for the
// link-local address, just under the advertised prefix instead of
// fe80::/64. Only /64 prefixes are meaningful here - the only length
// RFC 4862 SLAAC actually supports.
void ipv6_build_slaac_address(u8* prefix, u8 prefix_len, u8* out);

// Builds a 40-byte fixed IPv6 header (no options/fragmentation) at out[0..39].
void ipv6_build_header(u8* out, u8* src_ip6, u8* dst_ip6, u8 next_header, u16 payload_len);

// RFC 8200 8.1 pseudo-header (src+dst+upper-layer length+next header) +
// the real segment, summed via kernel/net/ip/ip.c's existing
// ip_checksum() (the one's-complement summing algorithm itself is
// identical for IPv4 and IPv6 - only the pseudo-header layout differs).
u16 ipv6_upper_layer_checksum(u8* src_ip6, u8* dst_ip6, u8 next_header, u8* segment, u16 segment_len);

bool ipv6_equals(u8* a, u8* b);

// The standard ff02::1:ffXX:XXXX solicited-node multicast address for a
// given target (RFC 4291 2.7.1) - the destination Neighbor Solicitation
// must be sent to.
void ipv6_solicited_node_multicast(u8* target_ip6, u8* out);

// RFC 2464: an IPv6 multicast address's Ethernet MAC is always
// 33:33:<last 4 bytes of the address>.
void ipv6_multicast_mac(u8* ip6_multicast_addr, u8* mac_out);

#pragma GCC visibility pop
