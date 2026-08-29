#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

bool icmp_ping(u8* target_ip, u16 identifier, u16 sequence);

#pragma GCC visibility pop
