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

// ---- Phase 2+ - real 802.11 hardware primitives, built on top of
// Phase 1's chip bring-up. Everything above this line is Phase 1
// (unchanged). Everything below is the hardware layer every later
// phase (kernel/net/rtw89/dot11.c's protocol layer, wpa2.c's
// handshake, wifi_manager.c's session/netdev bridge) is built on -
// see the plan file for the full real register/ring facts these
// implement.

// This chip's real EFUSE-stored MAC address register wasn't confirmed
// by this session's research (out of scope for the two research
// passes actually run) - honestly NOT guessed. Instead, a random
// locally-administered MAC (bit1 of the first byte set, bit0 clear -
// the standard "locally administered, unicast" convention) is
// generated once at rtw89_init() time via kernel/lib/rand.h's existing
// rand_next(). Functionally sufficient for real association (a real
// AP does not validate STA MAC vendor OUIs) - a real efuse read would
// be a strict improvement, not a correctness requirement, if ever
// revisited.
extern u8 g_rtw89_our_mac[6];

// Sends one real management frame (already a complete 802.11 frame,
// header through FCS-less body) via the CH8 management queue - wraps
// it in the real 48-byte TX WD header this queue requires (unlike
// Phase 1's CH12/FWCMD path, which correctly skips it). Returns false
// if the chip was never brought up.
bool rtw89_mgmt_send(const u8* frame, u32 len);

// Same shape, ACH0 (AC0/BE data queue) - used both for the WPA2
// handshake's own (real, spec-mandated unencrypted) EAPOL frames and
// Phase 6's real CCMP-encrypted data frames alike; the caller decides
// whether `frame`'s body is already CCMP-encrypted before calling
// this - this function only handles the TX WD/BD plumbing, never
// touches encryption itself.
bool rtw89_data_send(const u8* frame, u32 len);

// Real RPKT_TYPE values (RX descriptor dword0 bits 27:24) this driver
// distinguishes - 0 is every real over-the-air 802.11 frame this
// driver cares about (management + data alike); every other value
// (PPDU-stat/CSI/C2H/etc) is real but out of this driver's scope and
// silently skipped by rtw89_rx_poll() rather than ever being returned.
#define RTW89_RPKT_TYPE_WIFI 0

// Drains the RXQ ring (real polling protocol, no IRQ - see the plan
// file's own confirmed register-reuse facts) and returns the next
// real over-the-air 802.11 frame's bytes into `buf` (up to
// `max_len`), or 0 if the ring is currently empty. A single call
// only ever returns one frame (matching this whole codebase's
// one-thing-at-a-time synchronous driver style) - callers needing to
// drain multiple call this in a loop.
u32 rtw89_rx_poll(u8* buf, u32 max_len);

// Real host-side channel switch for 2.4GHz channels 1-13 (confirmed
// pure register writes via the RF SWSI bridge - no H2C, no firmware
// involvement - see the plan file). Returns false if the chip was
// never brought up, or the real PLL-lock poll never clears (a real,
// bounded wait, not `while(true)`).
bool rtw89_set_channel(u8 channel);

// ---- Phase 6 - real CCMP data-frame encryption + PN tracking, once
// a WPA2 handshake (kernel/net/rtw89/wpa2.c) has produced a real TK.
// Installs the negotiated CCMP TK + peer BSSID for this session's data
// path - `rtw89_send_data_frame`/`rtw89_recv_data_frame` below both
// use this installed state, real 802.11i CCMP nonce/AAD construction
// (see rtw89.c's own comment), reusing kernel/security/aes/aes_ccm.c.
// An OPEN network (no WPA2) never calls this - `rtw89_send_data_frame`
// then sends real, unencrypted QoS-Data frames instead (the real,
// honest behavior for an open network, not a missing feature).
void rtw89_install_ccmp_key(const u8 bssid[6], const u8 tk[16]);

// Real associated-BSSID storage for an OPEN network (no WPA2 handshake
// ever runs, so rtw89_install_ccmp_key never fires) - `rtw89_send_
// data_frame`'s addr1 needs a real BSSID either way. A network that
// does go through the handshake calls rtw89_install_ccmp_key instead,
// which sets both the BSSID and the key together.
void rtw89_set_bssid(const u8 bssid[6]);

// Real per-direction 48-bit PN (802.11i replay protection) - TX is one
// global monotonic counter, RX is tracked per-TID (this driver only
// ever uses TID 0, matching rtw89_send_data_frame's own real choice
// below) and real receives with a PN not strictly greater than the
// last-accepted one for that TID are rejected (a real security floor,
// not decorative - see the plan file's "production direction" note).
bool rtw89_send_data_frame(const u8 dst_mac[6], const u8* payload, u32 len);
// `src_mac_out` receives the real transmitter address (addr2) of
// whatever frame was received - the caller (kernel/net/rtw89/
// wifi_manager.c) uses this to reconstruct a plain Ethernet frame's
// source-MAC position. Returns the real decrypted (or, for an open
// network, plain) payload length, 0 if nothing new/valid arrived.
u32 rtw89_recv_data_frame(u8 src_mac_out[6], u8* payload_out, u32 max_len);

// True once rtw89_install_ccmp_key has run - kernel/net/rtw89/
// wifi_manager.c's own session-state machine checks this before
// deciding whether traffic is real (encrypted, associated+keyed) vs.
// still pending.
extern bool g_rtw89_ccmp_installed;

#pragma GCC visibility pop
