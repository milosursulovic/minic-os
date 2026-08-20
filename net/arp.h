#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

bool ip_equals(u8* a, u8* b);
bool arp_resolve(u8* target_ip, u8* mac_out);

#pragma GCC visibility pop
