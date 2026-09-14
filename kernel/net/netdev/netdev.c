// Real network-device dispatch layer - see netdev.h.

#include "netdev.h"
#include "../rtw89/wifi_manager.h"
#include "../rtw89/rtw89.h"
#include "../e1000/e1000.h"

bool netdev_send(const u8* eth_frame, u32 len) {
    if (g_wifi_state == WIFI_STATE_CONNECTED) {
        if (wifi_manager_send_eth(eth_frame, len)) {
            return true;
        }
        // Real WiFi send failure doesn't fall back to e1000 - the two
        // are different physical links with different peers; a
        // silent fallback here would deliver the frame nowhere useful.
        return false;
    }
    return e1000_send((u8*) eth_frame, (u16) len);
}

u16 netdev_receive(u8* buf, u16 max_len) {
    if (g_wifi_state == WIFI_STATE_CONNECTED) {
        return (u16) wifi_manager_receive_eth(buf, max_len);
    }
    return e1000_receive(buf, max_len);
}

void netdev_get_mac(u8* mac_out) {
    if (g_wifi_state == WIFI_STATE_CONNECTED) {
        int i = 0;
        while (i < 6) {
            mac_out[i] = g_rtw89_our_mac[i];
            i = i + 1;
        }
        return;
    }
    e1000_get_mac(mac_out);
}
