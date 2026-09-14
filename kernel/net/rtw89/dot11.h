#pragma once

#include "../../../types.h"

// Real, hand-written 802.11 management-frame layer (real-hardware
// driver arc item 5/5, Phases 3-4) - builds/parses real frames on top
// of rtw89.h's hardware primitives (Phase 2). Every field/subtype
// value here is the real, standard 802.11-2020 spec layout (frame
// control bit positions, fixed-field layouts, IE tag numbers) - a
// fixed public standard, not a design choice this project made.

#pragma GCC visibility push(hidden)

// Real 802.11 frame control subtype values this driver builds/parses
// (management frames only - Phase 6 builds QoS-Data frames directly,
// not through this table).
#define DOT11_SUBTYPE_ASSOC_REQ 0x0
#define DOT11_SUBTYPE_ASSOC_RESP 0x1
#define DOT11_SUBTYPE_PROBE_REQ 0x4
#define DOT11_SUBTYPE_PROBE_RESP 0x5
#define DOT11_SUBTYPE_BEACON 0x8
#define DOT11_SUBTYPE_AUTH 0xB
#define DOT11_TYPE_MGMT 0

// Real 24-byte 3-address 802.11 MAC header - every frame this layer
// builds/parses starts with exactly this shape.
typedef struct __attribute__((packed)) {
    u16 frame_control;
    u16 duration;
    u8 addr1[6];
    u8 addr2[6];
    u8 addr3[6];
    u16 seq_ctrl;
} dot11_hdr;

// Real scan-result shape (Phase 3) - deliberately no signal-strength
// field: this session's RX-descriptor research confirmed no RSSI
// field exists in the confirmed dwords - an honest omission, not a
// guessed/fabricated number. `has_rsn` (tag 48 IE present) is the real
// WPA2-vs-open distinction Phase 4/5 need to decide whether a 4-way
// handshake is required at all.
// Packed - this struct crosses the syscall 102 boundary as a raw array
// (kernel-written ring3 buffer, same pattern kernel/gfx/window/
// window.h's own input_event_t already uses) - proc/gui_toolkit/
// wifi.h mirrors this layout byte-for-byte for the ring3 GUI side.
typedef struct __attribute__((packed)) {
    bool used;
    char ssid[33];
    u8 ssid_len;
    u8 bssid[6];
    u8 channel;
    bool has_rsn;
} wifi_network;

#define WIFI_SCAN_MAX_NETWORKS 16

// Builds a real broadcast wildcard probe request (addr1/addr3 =
// ff:ff:ff:ff:ff:ff, addr2 = our_mac) into `out` - tag 0 (SSID,
// wildcard/zero-length) + tag 1 (Supported Rates, real basic 802.11b/g
// rate set) IEs, the minimum a real AP answers. Returns the real frame
// length written.
u32 dot11_build_probe_request(u8* out, u32 out_capacity, const u8 our_mac[6]);

// Walks a real beacon/probe-response frame's fixed fields (timestamp
// 8B + beacon interval 2B + capability info 2B) then its real IE list,
// filling `out` from tag 0 (SSID)/tag 3 (DS Parameter Set - channel)/
// tag 48 (RSN, presence only - decides has_rsn). Returns false if
// `frame` isn't long enough to be a real beacon/probe-response.
bool dot11_parse_beacon_or_probe_resp(const u8* frame, u32 len, const u8 bssid[6], wifi_network* out);

// Real host-driven scan (Phase 3): switches to each of channels
// {1,6,11} (rtw89_set_channel - the 3 real non-overlapping 2.4GHz
// channels almost every router uses), sends one probe request,
// drains the RX ring for a real, bounded ~150ms dwell (g_tick_count-
// based, same real timeout idiom kernel/net/tcp/tcp.c's own
// tcp_wait_segment already uses) collecting/deduping real beacons and
// probe responses by BSSID. Returns the real count found (0 if the
// chip was never brought up or nothing answered).
int dot11_scan(wifi_network* out, int max_networks);

// Real staged connect status (Phase 4) - distinguishes WHICH stage
// failed, the whole point of this multi-phase design (see the plan
// file's own stated diagnosability goal).
typedef enum {
    DOT11_AUTH_FAILED,
    DOT11_ASSOC_FAILED,
    DOT11_ASSOCIATED
} dot11_assoc_status;

// Real open-system authentication (algorithm=0, the real 3-frame
// exchange: request seq=1, response seq=2 status must be 0) followed
// by real association (capability info + supported rates +, for a
// `has_rsn` network, a real RSN IE advertising CCMP-128 pairwise + PSK
// AKM - the exact IE kernel/net/rtw89/wpa2.c's own 4-way handshake
// will actually satisfy). `bssid_out` receives the real negotiated
// BSSID (== net->bssid, returned for the caller's convenience since
// every later frame - EAPOL, data - needs it as addr1).
dot11_assoc_status dot11_connect_stage1(const wifi_network* net, const u8 our_mac[6], u8 bssid_out[6]);

// Real EAPOL send/receive over an UNENCRYPTED QoS-Data frame (real,
// spec-mandated - 802.1X EAPOL frames are always sent unprotected
// during the initial 4-way handshake, encryption isn't established
// until after message 4) - kernel/net/rtw89/wpa2.c's own handshake
// state machine calls these, never rtw89.h's raw rtw89_data_send
// directly, so the real 802.11 header + 802.2 LLC/SNAP (AA AA 03 00
// 00 00 88 8E - the standard EtherType-0x888E encapsulation) framing
// stays in one place.
bool dot11_send_eapol(const u8 bssid[6], const u8 our_mac[6], const u8* eapol, u32 eapol_len);

// Drains the RX ring (bounded, `timeout_ticks` g_tick_count-based, same
// idiom as dot11_scan) for the next real data frame carrying an EAPOL
// payload (LLC/SNAP EtherType 0x888E) from `bssid`, unwrapping the
// 802.11 header + LLC/SNAP framing before returning the real EAPOL
// bytes into `out`. Returns the real length (0 on timeout/nothing
// matched).
u32 dot11_recv_eapol(const u8 bssid[6], u8* out, u32 max_len, u64 timeout_ticks);

#pragma GCC visibility pop
