#pragma once

#include "../../../types.h"
#include "dot11.h"

// Real, hand-written WPA2-PSK/CCMP 4-way handshake (real-hardware
// driver arc item 5/5, Phase 5) - the standard, published 802.11i
// construction (PBKDF2-SHA1 for the PMK, the real PRF-384/HMAC-SHA1
// key expansion for the PTK, RFC 3394 AES-KW for the GTK) - a fixed
// spec, not a design choice made here. Built on kernel/net/rtw89/
// dot11.h's real EAPOL send/receive (dot11_send_eapol/recv_eapol),
// which itself sits on rtw89.h's hardware primitives.

#pragma GCC visibility push(hidden)

// Real CCMP PTK length (384 bits = 48 bytes: KCK[0:16] + KEK[16:32] +
// TK[32:48]) - NOT the 512-bit TKIP-shaped PTK (this driver never
// implements TKIP, CCMP-only per the RSN IE dot11.c's own assoc
// request always advertises).
#define WPA2_PTK_LEN 48
#define WPA2_KCK_OFFSET 0
#define WPA2_KEK_OFFSET 16
#define WPA2_TK_OFFSET 32

// Real PRF-384 (802.11i 8.5.1.1: HMAC-SHA1-based key expansion) over
// PBKDF2-SHA1's own PMK - `ptk_out` is `WPA2_PTK_LEN` bytes.
void wpa2_derive_ptk(const char* passphrase, u32 passphrase_len, const char* ssid, u32 ssid_len,
                      const u8 aa[6], const u8 spa[6], const u8 anonce[32], const u8 snonce[32],
                      u8 ptk_out[WPA2_PTK_LEN]);

// Real 802.1X EAPOL-Key wire format is fixed-field, big-endian (unlike
// 802.11's own little-endian frame_control/seq_ctrl, which happen to
// match x86 native order - EAPOL does NOT, so wpa2.c deliberately
// does NOT struct-cast raw bytes the way dot11.c does; it uses
// explicit big-endian byte read/write helpers instead). No public
// struct is needed here - every field offset is an internal detail of
// wpa2.c's own message builders/parsers.

typedef enum {
    WPA2_HANDSHAKE_OK,
    WPA2_HANDSHAKE_MIC_FAIL,
    WPA2_HANDSHAKE_TIMEOUT
} wpa2_status;

// Real 4-message state machine (see wpa2.c's own comment for the
// exact per-message shape) - on WPA2_HANDSHAKE_OK, `tk_out` holds the
// real, negotiated CCMP TK this session's data traffic (Phase 6) must
// use. WPA2_HANDSHAKE_MIC_FAIL specifically means the passphrase was
// wrong (the real, decisive distinction the GUI surfaces, per the plan
// file) - WPA2_HANDSHAKE_TIMEOUT means the AP never responded at all.
wpa2_status rtw89_wpa2_handshake(const wifi_network* net, const u8 bssid[6], const u8 our_mac[6],
                                  const char* passphrase, u32 passphrase_len, u8 tk_out[16]);

#pragma GCC visibility pop
