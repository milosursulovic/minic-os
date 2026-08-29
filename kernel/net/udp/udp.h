#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

bool udp_send(u8* target_ip, u16 dst_port, u16 src_port, u8* payload, u16 payload_len);
u16 udp_receive(u8* expected_src_ip, u16 expected_src_port, u16 expected_dst_port, u8* out, u16 max_len);

#pragma GCC visibility pop
