#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

void cmd_netconns(void);
void cmd_arp(void);
void cmd_ping(void);
void cmd_ipconfig(void);
void cmd_dns(void);
void cmd_tcp(void);

// Shared with commands/hardware.c's cmd_nic - both print a 6-byte MAC in
// the same colon-hex form.
void print_mac(u8* mac);

#pragma GCC visibility pop
