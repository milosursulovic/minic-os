#pragma once

#include "../../../types.h"
#include "dot11.h"
#include "wpa2.h"

// Real WiFi session-state facade (real-hardware driver arc item 5/5,
// Phase 8's backend) - the one file kernel/syscall/handlers/wifi.c
// (syscalls 101-105) and kernel/net/netdev/netdev.c (Phase 7) both
// talk to. Owns the real connection state machine on top of
// dot11.c's scan/auth/assoc and wpa2.c's handshake - named distinctly
// from proc/apps/wifi/wifi.c (the ring3 GUI, a completely separate
// program) to avoid confusion between the two.

#pragma GCC visibility push(hidden)

// Real staged connection state - every FAILED_* value is reachable
// and distinguishable (the whole point of this multi-phase design,
// see the plan file): FAILED_AUTH/FAILED_ASSOC come from dot11.c's own
// staged status, FAILED_WRONG_PASSWORD specifically means the 4-way
// handshake's MIC never verified (a real, decisive "bad password"
// signal, not a generic failure) - FAILED_TIMEOUT covers "the network
// wasn't found" or "the AP never responded at all".
typedef enum {
    WIFI_STATE_IDLE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED_AUTH,
    WIFI_STATE_FAILED_ASSOC,
    WIFI_STATE_FAILED_WRONG_PASSWORD,
    WIFI_STATE_FAILED_TIMEOUT
} wifi_state;

extern wifi_state g_wifi_state;
extern char g_wifi_connected_ssid[33];

// Real scan (dot11_scan), cached internally so wifi_manager_connect
// can look a network up by SSID alone (the syscall/GUI layer only
// ever passes SSID + passphrase strings, never a full wifi_network).
int wifi_manager_scan(wifi_network* out, int max_networks);

// Real, synchronous connect drive (auth -> assoc -> WPA2 handshake if
// the cached scan result's `has_rsn` is set) - updates g_wifi_state as
// it goes and returns the final state. `passphrase_len` may be 0 for
// an open network (the passphrase is then never read).
wifi_state wifi_manager_connect(const char* ssid, const char* passphrase, u32 passphrase_len);

// Real netdev bridge (Phase 7) - kernel/net/netdev/netdev.c calls
// these only while g_wifi_state == WIFI_STATE_CONNECTED. Translates a
// plain Ethernet frame (14B header: dst/src MAC + EtherType) to/from a
// real 802.11 QoS-Data frame with the standard 802.2 LLC/SNAP
// EtherType encapsulation, via rtw89.h's Phase 6 CCMP send/receive.
bool wifi_manager_send_eth(const u8* eth_frame, u32 len);
u32 wifi_manager_receive_eth(u8* buf, u32 max_len);

#pragma GCC visibility pop
