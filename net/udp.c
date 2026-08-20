// UDP: no connection state or retransmission, just a port-addressed datagram
// wrapper over ip.c.

#include "udp.h"
#include "ip.h"
#include "arp.h"
#include "e1000.h"
#include "../isr/isr.h"
#include "../sched/task.h"

static const u8 IP_PROTOCOL_UDP = 17;

// 12-byte pseudo-header (src/dst IP, protocol, UDP length) + UDP header+payload,
// binding the checksum to the addresses, not just the datagram bytes.
static u16 udp_checksum(u8* src_ip, u8* dst_ip, u8* udp_msg, u16 udp_len) {
    u8 buf[128];
    int i = 0;
    while (i < 4) {
        buf[i] = src_ip[i];
        buf[4 + i] = dst_ip[i];
        i = i + 1;
    }
    buf[8] = 0;
    buf[9] = IP_PROTOCOL_UDP;
    buf[10] = (u8) (udp_len >> 8);
    buf[11] = (u8) (udp_len & 0xFF);
    i = 0;
    while (i < (int) udp_len) {
        buf[12 + i] = udp_msg[i];
        i = i + 1;
    }
    u32 total_len = 12 + (u32) udp_len;
    if ((udp_len & 1) != 0) {
        buf[12 + udp_len] = 0;
        total_len = total_len + 1;
    }
    return ip_checksum(&buf[0], total_len);
}

// Caller must place the payload at out[8..8+payload_len) first - the checksum covers it.
static void udp_build_header(u8* out, u16 src_port, u16 dst_port, u16 payload_len, u8* src_ip, u8* dst_ip) {
    u16 udp_len = 8 + payload_len;
    out[0] = (u8) (src_port >> 8);
    out[1] = (u8) (src_port & 0xFF);
    out[2] = (u8) (dst_port >> 8);
    out[3] = (u8) (dst_port & 0xFF);
    out[4] = (u8) (udp_len >> 8);
    out[5] = (u8) (udp_len & 0xFF);
    out[6] = 0;
    out[7] = 0;
    u16 csum = udp_checksum(src_ip, dst_ip, out, udp_len);
    out[6] = (u8) (csum >> 8);
    out[7] = (u8) (csum & 0xFF);
}

bool udp_send(u8* target_ip, u16 dst_port, u16 src_port, u8* payload, u16 payload_len) {
    u8 dest_mac[6];
    if (!arp_resolve(target_ip, &dest_mac[0])) {
        return false;
    }
    ip_init();
    u8 src_mac[6];
    e1000_get_mac(&src_mac[0]);

    u8 frame[128];
    int i = 0;
    while (i < 128) {
        frame[i] = 0;
        i = i + 1;
    }
    i = 0;
    while (i < 6) {
        frame[i] = dest_mac[i];
        i = i + 1;
    }
    i = 0;
    while (i < 6) {
        frame[6 + i] = src_mac[i];
        i = i + 1;
    }
    frame[12] = 0x08;
    frame[13] = 0x00;

    ip_build_header(&frame[14], &g_my_ip[0], target_ip, IP_PROTOCOL_UDP, 8 + payload_len);

    i = 0;
    while (i < (int) payload_len) {
        frame[34 + 8 + i] = payload[i];
        i = i + 1;
    }
    udp_build_header(&frame[34], src_port, dst_port, payload_len, &g_my_ip[0], target_ip);

    u16 frame_len = 42 + payload_len;
    return e1000_send(&frame[0], frame_len);
}

// Tick-bounded poll for a datagram matching EtherType/protocol/source IP/ports.
// Returns 0 if nothing matching arrives before the timeout.
u16 udp_receive(u8* expected_src_ip, u16 expected_src_port, u16 expected_dst_port, u8* out, u16 max_len) {
    u8 reply[160];
    u64 start_tick = g_tick_count;
    while (g_tick_count - start_tick < 2000) {
        u16 len = e1000_receive(&reply[0], 160);
        if (len > 0) {
            bool is_ip = reply[12] == 0x08 && reply[13] == 0x00;
            if (is_ip) {
                u8 proto = reply[14 + 9];
                bool is_udp = proto == IP_PROTOCOL_UDP;
                bool src_matches = reply[14 + 12] == expected_src_ip[0]
                    && reply[14 + 13] == expected_src_ip[1]
                    && reply[14 + 14] == expected_src_ip[2]
                    && reply[14 + 15] == expected_src_ip[3];
                u16 src_port = (((u16) reply[34]) << 8) | ((u16) reply[35]);
                u16 dst_port = (((u16) reply[36]) << 8) | ((u16) reply[37]);
                if (is_udp && src_matches && src_port == expected_src_port && dst_port == expected_dst_port) {
                    u16 udp_len = (((u16) reply[38]) << 8) | ((u16) reply[39]);
                    u16 payload_len = udp_len - 8;
                    u16 copy_len = payload_len;
                    if (copy_len > max_len) {
                        copy_len = max_len;
                    }
                    int j = 0;
                    while (j < (int) copy_len) {
                        out[j] = reply[42 + j];
                        j = j + 1;
                    }
                    return payload_len;
                }
            }
        }
        yield();  // this runs on a background worker task now
    }
    return 0;
}
