#pragma once

#include "../../../types.h"

// Real network-device dispatch layer (real-hardware driver arc item
// 5/5, Phase 7) - same shape as this arc's own kernel/fs/blockdev/
// blockdev.c and kernel/drivers/usb/usbhc.c precedent: every existing
// IP-stack caller (arp.c/icmp.c/icmp6.c/ndp.c/tcp.c/udp.c/dhcp.c, 13
// real call sites, confirmed via repo grep) currently calls e1000_send/
// e1000_receive directly - this file becomes the one seam between
// them and either real NIC, so none of those 7 files need any
// awareness of WiFi at all beyond the mechanical rename below.
//
// Real, zero-regression fallback (never a silent replace - the
// standing rule this whole arc has followed since item 2's own
// revert): if WiFi isn't connected, every call falls through to the
// existing, unchanged e1000_send/e1000_receive - identical behavior
// to before this file existed, for every QEMU test config and every
// real-hardware boot where WiFi never associates.

#pragma GCC visibility push(hidden)

bool netdev_send(const u8* eth_frame, u32 len);
u16 netdev_receive(u8* buf, u16 max_len);

// Real, necessary consequence of dispatching between two physically
// different links (not in the original e1000_send/receive-only call
// sites, found while rewiring them): every caller here also builds
// its own outgoing frame's source-MAC field via what used to be a
// direct e1000_get_mac() call - while WiFi is connected, that MUST be
// this session's real rtw89 MAC (g_rtw89_our_mac), not e1000's,  or
// outgoing frames would claim the wrong source address. Same
// fallback rule as netdev_send/receive: e1000's real MAC otherwise.
void netdev_get_mac(u8* mac_out);

#pragma GCC visibility pop
