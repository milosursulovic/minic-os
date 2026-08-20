// ICMP echo (ping) only - no other message types.

#include "icmp.h"
#include "ip.h"
#include "arp.h"
#include "e1000.h"
#include "../isr/isr.h"
#include "../sched/task.h"

static const u8 ICMP_ECHO_REQUEST = 8;
static const u8 ICMP_ECHO_REPLY = 0;
static const u8 IP_PROTOCOL_ICMP = 1;

// Resolves target_ip's MAC, sends an echo request, and polls (tick-bounded)
// for a reply matching EtherType/protocol/source IP/type/identifier/sequence.
bool icmp_ping(u8* target_ip, u16 identifier, u16 sequence) {
    u8 dest_mac[6];
    if (!arp_resolve(target_ip, &dest_mac[0])) {
        return false;
    }
    ip_init();
    u8 src_mac[6];
    e1000_get_mac(&src_mac[0]);

    // 8-byte ICMP header + a small, fixed 4-byte payload.
    u8 icmp_msg[12];
    icmp_msg[0] = ICMP_ECHO_REQUEST;
    icmp_msg[1] = 0;
    icmp_msg[2] = 0;   // checksum placeholder
    icmp_msg[3] = 0;
    icmp_msg[4] = (u8) (identifier >> 8);
    icmp_msg[5] = (u8) (identifier & 0xFF);
    icmp_msg[6] = (u8) (sequence >> 8);
    icmp_msg[7] = (u8) (sequence & 0xFF);
    icmp_msg[8] = (u8) 'p';
    icmp_msg[9] = (u8) 'i';
    icmp_msg[10] = (u8) 'n';
    icmp_msg[11] = (u8) 'g';
    u16 icmp_csum = ip_checksum(&icmp_msg[0], 12);
    icmp_msg[2] = (u8) (icmp_csum >> 8);
    icmp_msg[3] = (u8) (icmp_csum & 0xFF);

    // Ethernet (14) + IPv4 (20) + ICMP (12) = 46 bytes.
    u8 frame[64];
    int i = 0;
    while (i < 64) {
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
    frame[12] = 0x08;   // EtherType = 0x0800 (IPv4)
    frame[13] = 0x00;

    ip_build_header(&frame[14], &g_my_ip[0], target_ip, IP_PROTOCOL_ICMP, 12);

    i = 0;
    while (i < 12) {
        frame[34 + i] = icmp_msg[i];
        i = i + 1;
    }

    if (!e1000_send(&frame[0], 46)) {
        return false;
    }

    u8 reply[128];
    u64 start_tick = g_tick_count;
    while (g_tick_count - start_tick < 2000) {
        u16 len = e1000_receive(&reply[0], 128);
        if (len > 0) {
            bool is_ip = reply[12] == 0x08 && reply[13] == 0x00;
            if (is_ip) {
                // Assumes IHL=5 (no options) - safe since we never send options.
                u8 proto = reply[14 + 9];
                bool is_icmp = proto == IP_PROTOCOL_ICMP;
                bool src_matches = reply[14 + 12] == target_ip[0]
                    && reply[14 + 13] == target_ip[1]
                    && reply[14 + 14] == target_ip[2]
                    && reply[14 + 15] == target_ip[3];
                u8 icmp_type = reply[34];
                bool is_echo_reply = icmp_type == ICMP_ECHO_REPLY;
                u16 reply_id = (((u16) reply[38]) << 8) | ((u16) reply[39]);
                u16 reply_seq = (((u16) reply[40]) << 8) | ((u16) reply[41]);
                if (is_icmp && src_matches && is_echo_reply
                    && reply_id == identifier && reply_seq == sequence) {
                    return true;
                }
            }
        }
        yield();  // cooperative, not just timer-forced - matters when this runs on a background worker task
    }
    return false;
}
