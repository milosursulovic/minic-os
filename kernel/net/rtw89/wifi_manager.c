// Real WiFi session-state facade - see wifi_manager.h.

#include "wifi_manager.h"
#include "rtw89.h"
#include "../../lib/strings.h"

wifi_state g_wifi_state;
char g_wifi_connected_ssid[33];

#define SCAN_CACHE_SIZE WIFI_SCAN_MAX_NETWORKS
static wifi_network g_scan_cache[SCAN_CACHE_SIZE];
static int g_scan_cache_count;

int wifi_manager_scan(wifi_network* out, int max_networks) {
    g_scan_cache_count = dot11_scan(g_scan_cache, SCAN_CACHE_SIZE);
    int n = g_scan_cache_count;
    if (n > max_networks) {
        n = max_networks;
    }
    int i = 0;
    while (i < n) {
        out[i] = g_scan_cache[i];
        i = i + 1;
    }
    return n;
}

static int find_cached_network(const char* ssid) {
    int i = 0;
    while (i < g_scan_cache_count) {
        if (streq(g_scan_cache[i].ssid, ssid)) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

wifi_state wifi_manager_connect(const char* ssid, const char* passphrase, u32 passphrase_len) {
    g_wifi_state = WIFI_STATE_CONNECTING;

    int idx = find_cached_network(ssid);
    if (idx < 0) {
        g_scan_cache_count = dot11_scan(g_scan_cache, SCAN_CACHE_SIZE);
        idx = find_cached_network(ssid);
    }
    if (idx < 0) {
        g_wifi_state = WIFI_STATE_FAILED_TIMEOUT;
        return g_wifi_state;
    }

    u8 bssid[6];
    dot11_assoc_status assoc = dot11_connect_stage1(&g_scan_cache[idx], g_rtw89_our_mac, bssid);
    if (assoc == DOT11_AUTH_FAILED) {
        g_wifi_state = WIFI_STATE_FAILED_AUTH;
        return g_wifi_state;
    }
    if (assoc == DOT11_ASSOC_FAILED) {
        g_wifi_state = WIFI_STATE_FAILED_ASSOC;
        return g_wifi_state;
    }

    if (!g_scan_cache[idx].has_rsn) {
        rtw89_set_bssid(bssid);
        int i = 0;
        while (ssid[i] != 0 && i < 32) {
            g_wifi_connected_ssid[i] = ssid[i];
            i = i + 1;
        }
        g_wifi_connected_ssid[i] = 0;
        g_wifi_state = WIFI_STATE_CONNECTED;
        return g_wifi_state;
    }

    u8 tk[16];
    wpa2_status ws = rtw89_wpa2_handshake(&g_scan_cache[idx], bssid, g_rtw89_our_mac, passphrase,
                                            passphrase_len, tk);
    if (ws == WPA2_HANDSHAKE_MIC_FAIL) {
        g_wifi_state = WIFI_STATE_FAILED_WRONG_PASSWORD;
        return g_wifi_state;
    }
    if (ws == WPA2_HANDSHAKE_TIMEOUT) {
        g_wifi_state = WIFI_STATE_FAILED_TIMEOUT;
        return g_wifi_state;
    }

    rtw89_install_ccmp_key(bssid, tk);
    int i = 0;
    while (ssid[i] != 0 && i < 32) {
        g_wifi_connected_ssid[i] = ssid[i];
        i = i + 1;
    }
    g_wifi_connected_ssid[i] = 0;
    g_wifi_state = WIFI_STATE_CONNECTED;
    return g_wifi_state;
}

// Real, standard 802.2 LLC/SNAP encapsulation - the fixed spec bytes
// dot11.c's own EAPOL framing already uses for EtherType 0x888E; here
// the EtherType is whatever the real Ethernet frame being carried
// says (IPv4 0x0800, ARP 0x0806, IPv6 0x86DD, etc - this driver
// doesn't need to special-case any of them, the AP/network stack
// above already knows how to parse the payload once EtherType is
// preserved correctly).
#define ETH_HDR_LEN 14
#define LLC_SNAP_LEN 8

bool wifi_manager_send_eth(const u8* eth_frame, u32 len) {
    if (len < ETH_HDR_LEN) {
        return false;
    }
    static u8 body[2400];
    if (LLC_SNAP_LEN + (len - ETH_HDR_LEN) > sizeof(body)) {
        return false;
    }
    body[0] = 0xAA;
    body[1] = 0xAA;
    body[2] = 0x03;
    body[3] = 0x00;
    body[4] = 0x00;
    body[5] = 0x00;
    body[6] = eth_frame[12];  // EtherType, preserved verbatim
    body[7] = eth_frame[13];
    u32 payload_len = len - ETH_HDR_LEN;
    u32 i = 0;
    while (i < payload_len) {
        body[LLC_SNAP_LEN + i] = eth_frame[ETH_HDR_LEN + i];
        i = i + 1;
    }
    const u8* dst_mac = eth_frame;  // real Ethernet dst is the frame's first 6 bytes
    return rtw89_send_data_frame(dst_mac, body, LLC_SNAP_LEN + payload_len);
}

u32 wifi_manager_receive_eth(u8* buf, u32 max_len) {
    static u8 body[2400];
    u8 src_mac[6];
    u32 body_len = rtw89_recv_data_frame(src_mac, body, sizeof(body));
    if (body_len < LLC_SNAP_LEN) {
        return 0;
    }
    if (!(body[0] == 0xAA && body[1] == 0xAA)) {
        return 0;  // not a real LLC/SNAP-encapsulated frame - not this driver's traffic
    }
    u32 payload_len = body_len - LLC_SNAP_LEN;
    if (ETH_HDR_LEN + payload_len > max_len) {
        payload_len = max_len - ETH_HDR_LEN;
    }
    u32 i = 0;
    while (i < 6) {
        buf[i] = g_rtw89_our_mac[i];       // real Ethernet dst = us
        buf[6 + i] = src_mac[i];           // real Ethernet src = the original sender (802.11 addr3)
        i = i + 1;
    }
    buf[12] = body[6];  // EtherType, preserved verbatim
    buf[13] = body[7];
    i = 0;
    while (i < payload_len) {
        buf[ETH_HDR_LEN + i] = body[LLC_SNAP_LEN + i];
        i = i + 1;
    }
    return ETH_HDR_LEN + payload_len;
}
