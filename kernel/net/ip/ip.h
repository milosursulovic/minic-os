#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

extern u8 g_my_ip[4];
// QEMU SLIRP's fixed default gateway/DNS-relay addresses - one real
// source of truth (ip_init() sets all three together), instead of the
// same two values re-hardcoded separately in shell.c and net/tcp/tcp.c.
extern u8 g_gateway_ip[4];
extern u8 g_dns_server_ip[4];

void ip_init(void);
u16 ip_checksum(u8* data, u32 len);
void ip_build_header(u8* out, u8* src_ip, u8* dst_ip, u8 protocol, u16 payload_len);

#pragma GCC visibility pop
