#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

#define TX_RING_SIZE 8
#define RX_RING_SIZE 8

bool e1000_init(void);
void e1000_get_mac(u8* mac_out);
bool e1000_link_up(void);
u32 e1000_read_reg(u32 offset);
void e1000_write_reg(u32 offset, u32 value);
bool e1000_init_rings(void);
bool e1000_send(u8* data, u16 len);
u16 e1000_receive(u8* out, u16 max_len);

#pragma GCC visibility pop
