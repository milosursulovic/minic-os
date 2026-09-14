#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real, hand-written Realtek RTL8852BE WiFi driver - real-hardware
// driver arc item 5/5, Phase 1 (firmware upload + chip bring-up only -
// see the plan file / project memory for the full, multi-phase real
// scope: scanning/association/WPA2/IP-stack integration are separate,
// later phases). The ONE narrow, documented exception to this
// project's hand-written-only rule (CLAUDE.md, 2026-09-14): the ~1MB
// vendor firmware blob (rtw89_fw_blob.s) is uploaded to the chip
// verbatim as inert data - it runs only on the chip's own embedded
// core, never on this kernel's CPU. Every register/DMA/protocol byte
// this file itself sends is 100% hand-written, based on reading the
// real (GPL, public) Linux rtw89 driver source as a register-level
// reference no vendor publishes - not copied or ported.
//
// No QEMU emulation exists for this exact chip - unlike every other
// driver in this arc, this one cannot be verified via the fast
// `-kernel` dev loop at all. Real verification needs `build.sh iso` +
// booting on this dev laptop's actual hardware.

// PCI-discovers the real RTL8852BE (vendor 0x10EC, device 0xB852), maps
// its 1MB MMIO BAR0, runs the real power-on + WCPU-arm sequence, sets
// up the CH12 "FWCMD" TX DMA ring, uploads the embedded firmware blob
// in chunked H2C packets, and polls for WCPU_FW_INIT_RDY. Returns false
// if the chip isn't present, or any bring-up step fails - a real,
// honest "nothing to do" matching every other driver's own convention.
bool rtw89_init(void);

// The real 3-bit status last read from R_AX_WCPU_FW_CTRL's status
// field (see rtw89.c) - 0=initial,1=ongoing,2=checksum fail,3=security
// fail,4=CV mismatch,6=WCPU_FWDL_RDY,7=WCPU_FW_INIT_RDY (success).
// Exposed so a shell command can print the real, decisive result
// rather than just "didn't crash".
extern u8 g_rtw89_fwdl_status;

#pragma GCC visibility pop
