// Real, hand-written 802.11 management-frame layer - see dot11.h.

#include "dot11.h"
#include "rtw89.h"
#include "../../isr/isr.h"

static void mac_copy(u8* dst, const u8* src) {
    int i = 0;
    while (i < 6) {
        dst[i] = src[i];
        i = i + 1;
    }
}

static bool mac_eq(const u8* a, const u8* b) {
    int i = 0;
    while (i < 6) {
        if (a[i] != b[i]) {
            return false;
        }
        i = i + 1;
    }
    return true;
}

static void write_u16le(u8* p, u16 v) {
    p[0] = (u8) v;
    p[1] = (u8) (v >> 8);
}

static u16 read_u16le(const u8* p) {
    return (u16) ((u16) p[0] | ((u16) p[1] << 8));
}

static u32 append_ie(u8* out, u32 pos, u8 tag, const u8* data, u8 len) {
    out[pos] = tag;
    out[pos + 1] = len;
    u32 i = 0;
    while (i < len) {
        out[pos + 2 + i] = data[i];
        i = i + 1;
    }
    return pos + 2 + len;
}

static u16 g_dot11_seq;

static const u8 BROADCAST_MAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

// Real basic 802.11b/g rate set, standard "basic rate" encoding
// (rate in 500kbps units | 0x80 to mark it mandatory) - 1/2/5.5/11 Mbps.
static const u8 BASIC_RATES[4] = {0x82, 0x84, 0x8B, 0x96};

u32 dot11_build_probe_request(u8* out, u32 out_capacity, const u8 our_mac[6]) {
    if (out_capacity < 24 + 2 + 2 + 4) {
        return 0;
    }
    dot11_hdr* h = (dot11_hdr*) out;
    h->frame_control = (u16) ((DOT11_SUBTYPE_PROBE_REQ << 4) | (DOT11_TYPE_MGMT << 2));
    h->duration = 0;
    mac_copy(h->addr1, BROADCAST_MAC);
    mac_copy(h->addr2, our_mac);
    mac_copy(h->addr3, BROADCAST_MAC);
    h->seq_ctrl = (u16) (g_dot11_seq << 4);
    g_dot11_seq = (u16) (g_dot11_seq + 1);

    u32 pos = 24;
    pos = append_ie(out, pos, 0, 0, 0);  // wildcard SSID
    pos = append_ie(out, pos, 1, BASIC_RATES, 4);
    return pos;
}

bool dot11_parse_beacon_or_probe_resp(const u8* frame, u32 len, const u8 bssid[6], wifi_network* out) {
    if (len < 24 + 12) {
        return false;
    }
    out->used = true;
    mac_copy(out->bssid, bssid);
    out->ssid_len = 0;
    out->ssid[0] = 0;
    out->channel = 0;
    out->has_rsn = false;

    u32 pos = 24 + 12;  // skip fixed fields (timestamp+beacon interval+cap info)
    while (pos + 2 <= len) {
        u8 tag = frame[pos];
        u8 ie_len = frame[pos + 1];
        u32 data_off = pos + 2;
        if (data_off + ie_len > len) {
            break;
        }
        if (tag == 0) {
            u8 n = ie_len;
            if (n > 32) {
                n = 32;
            }
            u32 i = 0;
            while (i < n) {
                out->ssid[i] = (char) frame[data_off + i];
                i = i + 1;
            }
            out->ssid[n] = 0;
            out->ssid_len = n;
        } else if (tag == 3 && ie_len >= 1) {
            out->channel = frame[data_off];
        } else if (tag == 48) {
            out->has_rsn = true;
        }
        pos = data_off + ie_len;
    }
    return true;
}

int dot11_scan(wifi_network* out, int max_networks) {
    static const u8 channels[3] = {1, 6, 11};
    int found = 0;
    int ci = 0;
    while (ci < 3) {
        if (rtw89_set_channel(channels[ci])) {
            u8 probe[40];
            u32 probe_len = dot11_build_probe_request(probe, sizeof(probe), g_rtw89_our_mac);
            if (probe_len > 0) {
                rtw89_mgmt_send(probe, probe_len);
            }

            u64 start = g_tick_count;
            while (g_tick_count - start < 15) {  // ~150ms real dwell (100Hz PIT)
                u8 buf[2400];
                u32 rlen = rtw89_rx_poll(buf, sizeof(buf));
                if (rlen < 24 + 12) {
                    continue;
                }
                const dot11_hdr* h = (const dot11_hdr*) buf;
                u16 fc = h->frame_control;
                u16 type = (u16) ((fc >> 2) & 0x3);
                u16 subtype = (u16) ((fc >> 4) & 0xF);
                if (type != DOT11_TYPE_MGMT) {
                    continue;
                }
                if (subtype != DOT11_SUBTYPE_BEACON && subtype != DOT11_SUBTYPE_PROBE_RESP) {
                    continue;
                }
                bool dup = false;
                int i = 0;
                while (i < found) {
                    if (mac_eq(out[i].bssid, h->addr3)) {
                        dup = true;
                        break;
                    }
                    i = i + 1;
                }
                if (dup || found >= max_networks) {
                    continue;
                }
                wifi_network net;
                if (dot11_parse_beacon_or_probe_resp(buf, rlen, h->addr3, &net)) {
                    out[found] = net;
                    found = found + 1;
                }
            }
        }
        ci = ci + 1;
    }
    return found;
}

static u32 build_auth_request(u8* out, u32 out_capacity, const u8 our_mac[6], const u8 bssid[6]) {
    if (out_capacity < 24 + 4) {
        return 0;
    }
    dot11_hdr* h = (dot11_hdr*) out;
    h->frame_control = (u16) ((DOT11_SUBTYPE_AUTH << 4) | (DOT11_TYPE_MGMT << 2));
    h->duration = 0;
    mac_copy(h->addr1, bssid);
    mac_copy(h->addr2, our_mac);
    mac_copy(h->addr3, bssid);
    h->seq_ctrl = (u16) (g_dot11_seq << 4);
    g_dot11_seq = (u16) (g_dot11_seq + 1);

    write_u16le(&out[24], 0);  // Algorithm Number = 0 (open system)
    write_u16le(&out[26], 1);  // Auth Transaction Sequence Number = 1 (request)
    return 28;
}

static u32 build_assoc_request(u8* out, u32 out_capacity, const u8 our_mac[6], const wifi_network* net) {
    if (out_capacity < 24 + 4 + 2 + 2 + 4 + 2 + 20) {
        return 0;
    }
    dot11_hdr* h = (dot11_hdr*) out;
    h->frame_control = (u16) ((DOT11_SUBTYPE_ASSOC_REQ << 4) | (DOT11_TYPE_MGMT << 2));
    h->duration = 0;
    mac_copy(h->addr1, net->bssid);
    mac_copy(h->addr2, our_mac);
    mac_copy(h->addr3, net->bssid);
    h->seq_ctrl = (u16) (g_dot11_seq << 4);
    g_dot11_seq = (u16) (g_dot11_seq + 1);

    u16 cap_info = 0x0001 | (net->has_rsn ? 0x0010 : 0);  // ESS + Privacy (if secured)
    write_u16le(&out[24], cap_info);
    write_u16le(&out[26], 10);  // Listen Interval

    u32 pos = 28;
    pos = append_ie(out, pos, 0, (const u8*) net->ssid, net->ssid_len);
    pos = append_ie(out, pos, 1, BASIC_RATES, 4);
    if (net->has_rsn) {
        // Real RSN IE this driver actually satisfies: version=1,
        // group cipher=CCMP (00-0F-AC:4), 1 pairwise cipher=CCMP,
        // 1 AKM=PSK (00-0F-AC:2), RSN capabilities=0.
        u8 rsn[20] = {
            0x01, 0x00,                          // version
            0x00, 0x0F, 0xAC, 0x04,               // group cipher: CCMP
            0x01, 0x00,                           // pairwise cipher count
            0x00, 0x0F, 0xAC, 0x04,               // pairwise cipher: CCMP
            0x01, 0x00,                           // AKM count
            0x00, 0x0F, 0xAC, 0x02,               // AKM: PSK
            0x00, 0x00                            // RSN capabilities
        };
        pos = append_ie(out, pos, 48, rsn, 20);
    }
    return pos;
}

dot11_assoc_status dot11_connect_stage1(const wifi_network* net, const u8 our_mac[6], u8 bssid_out[6]) {
    mac_copy(bssid_out, net->bssid);

    u8 frame[300];
    u32 len = build_auth_request(frame, sizeof(frame), our_mac, net->bssid);
    if (len == 0 || !rtw89_mgmt_send(frame, len)) {
        return DOT11_AUTH_FAILED;
    }

    bool auth_ok = false;
    u64 start = g_tick_count;
    while (g_tick_count - start < 30) {  // ~300ms
        u8 buf[300];
        u32 rlen = rtw89_rx_poll(buf, sizeof(buf));
        if (rlen < 30) {
            continue;
        }
        const dot11_hdr* h = (const dot11_hdr*) buf;
        u16 fc = h->frame_control;
        u16 type = (u16) ((fc >> 2) & 0x3);
        u16 subtype = (u16) ((fc >> 4) & 0xF);
        if (type != DOT11_TYPE_MGMT || subtype != DOT11_SUBTYPE_AUTH) {
            continue;
        }
        u16 seq = read_u16le(&buf[26]);
        u16 status = read_u16le(&buf[28]);
        if (seq == 2) {
            auth_ok = (status == 0);
            break;
        }
    }
    if (!auth_ok) {
        return DOT11_AUTH_FAILED;
    }

    len = build_assoc_request(frame, sizeof(frame), our_mac, net);
    if (len == 0 || !rtw89_mgmt_send(frame, len)) {
        return DOT11_ASSOC_FAILED;
    }

    bool assoc_ok = false;
    start = g_tick_count;
    while (g_tick_count - start < 30) {
        u8 buf[300];
        u32 rlen = rtw89_rx_poll(buf, sizeof(buf));
        if (rlen < 30) {
            continue;
        }
        const dot11_hdr* h = (const dot11_hdr*) buf;
        u16 fc = h->frame_control;
        u16 type = (u16) ((fc >> 2) & 0x3);
        u16 subtype = (u16) ((fc >> 4) & 0xF);
        if (type != DOT11_TYPE_MGMT || subtype != DOT11_SUBTYPE_ASSOC_RESP) {
            continue;
        }
        u16 status = read_u16le(&buf[26]);
        assoc_ok = (status == 0);
        break;
    }
    return assoc_ok ? DOT11_ASSOCIATED : DOT11_ASSOC_FAILED;
}

// Real, standard 802.2 LLC/SNAP encapsulation for EtherType 0x888E
// (802.1X/EAPOL) - fixed spec bytes, not chosen here.
static const u8 LLC_SNAP_EAPOL[8] = {0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8E};

bool dot11_send_eapol(const u8 bssid[6], const u8 our_mac[6], const u8* eapol, u32 eapol_len) {
    u8 frame[600];
    if (24 + 8 + eapol_len > sizeof(frame)) {
        return false;
    }
    dot11_hdr* h = (dot11_hdr*) frame;
    h->frame_control = (u16) ((0 << 4) | (2 << 2) | (1 << 8));  // plain Data, toDS=1
    h->duration = 0;
    mac_copy(h->addr1, bssid);
    mac_copy(h->addr2, our_mac);
    mac_copy(h->addr3, bssid);
    h->seq_ctrl = (u16) (g_dot11_seq << 4);
    g_dot11_seq = (u16) (g_dot11_seq + 1);

    u32 i = 0;
    while (i < 8) {
        frame[24 + i] = LLC_SNAP_EAPOL[i];
        i = i + 1;
    }
    i = 0;
    while (i < eapol_len) {
        frame[32 + i] = eapol[i];
        i = i + 1;
    }
    return rtw89_data_send(frame, 32 + eapol_len);
}

u32 dot11_recv_eapol(const u8 bssid[6], u8* out, u32 max_len, u64 timeout_ticks) {
    u64 start = g_tick_count;
    while (g_tick_count - start < timeout_ticks) {
        u8 buf[600];
        u32 len = rtw89_rx_poll(buf, sizeof(buf));
        if (len < 24 + 8) {
            continue;
        }
        const dot11_hdr* h = (const dot11_hdr*) buf;
        u16 fc = h->frame_control;
        u16 type = (u16) ((fc >> 2) & 0x3);
        if (type != 2) {  // Data
            continue;
        }
        if (!mac_eq(h->addr2, bssid) && !mac_eq(h->addr3, bssid)) {
            continue;
        }
        u16 subtype = (u16) ((fc >> 4) & 0xF);
        u32 hdr_len = 24;
        if ((subtype & 0x8) != 0) {  // QoS Data - 2 extra bytes
            hdr_len = hdr_len + 2;
        }
        if (len < hdr_len + 8) {
            continue;
        }
        const u8* llc = &buf[hdr_len];
        bool is_eapol = (llc[0] == 0xAA && llc[1] == 0xAA && llc[6] == 0x88 && llc[7] == 0x8E);
        if (!is_eapol) {
            continue;
        }
        u32 eapol_len = len - hdr_len - 8;
        if (eapol_len > max_len) {
            eapol_len = max_len;
        }
        u32 i = 0;
        while (i < eapol_len) {
            out[i] = buf[hdr_len + 8 + i];
            i = i + 1;
        }
        return eapol_len;
    }
    return 0;
}
