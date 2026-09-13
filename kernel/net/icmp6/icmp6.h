#pragma once

#include "../../../types.h"

// ICMPv6 echo (ping6) only - no other message types. Mirrors
// kernel/net/icmp/icmp.c's own ICMPv4 shape exactly.

#pragma GCC visibility push(hidden)

bool icmp6_ping(u8* target_ip6, u16 identifier, u16 sequence);

#pragma GCC visibility pop
