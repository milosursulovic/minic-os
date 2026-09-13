// See icmp6.h's own comment for scope.

#include "icmp6.h"
#include "../ipv6/ipv6.h"
#include "../ndp/ndp.h"
#include "../e1000/e1000.h"
#include "../../isr/isr.h"
#include "../../sched/task.h"

static const u8 IP_PROTOCOL_ICMPV6 = 58;
static const u8 ICMPV6_ECHO_REQUEST = 128;
static const u8 ICMPV6_ECHO_REPLY = 129;

static bool is_link_local(u8* ip6) {
    return ip6[0] == 0xFE && (ip6[1] & 0xC0) == 0x80;
}

// Resolves target_ip6's MAC, sends an echo request, and polls (tick-bounded)
// for a reply matching EtherType/next-header/source address/type/identifier/sequence.
bool icmp6_ping(u8* target_ip6, u16 identifier, u16 sequence) {
    ipv6_init_link_local();

    u8* src_ip6 = &g_my_ipv6_link_local[0];
    if (!is_link_local(target_ip6) && g_ipv6_global_valid) {
        src_ip6 = &g_my_ipv6_global[0];
    }

    // Real routing, not always a direct NDP resolve for target_ip6: an
    // off-link (non-link-local) target must go via the default router's
    // own MAC while the IPv6 header's destination stays the real target -
    // same "route via the gateway's link-layer address" pattern
    // kernel/net/icmp/icmp.c's own icmp_ping() already uses for IPv4.
    u8 dest_mac[6];
    bool resolved;
    if (is_link_local(target_ip6)) {
        resolved = ndp_resolve(target_ip6, &dest_mac[0]);
    } else if (g_ipv6_global_valid) {
        resolved = ndp_resolve(&g_ipv6_default_router[0], &dest_mac[0]);
    } else {
        resolved = false;  // off-link and no default router - can't route at all
    }
    if (!resolved) {
        return false;
    }

    u8 src_mac[6];
    e1000_get_mac(&src_mac[0]);

    // 4-byte ICMPv6 header + a small, fixed 4-byte payload (same shape
    // kernel/net/icmp/icmp.c's own ICMPv4 echo uses).
    u8 icmp6_msg[12];
    icmp6_msg[0] = ICMPV6_ECHO_REQUEST;
    icmp6_msg[1] = 0;
    icmp6_msg[2] = 0;   // checksum placeholder
    icmp6_msg[3] = 0;
    icmp6_msg[4] = (u8) (identifier >> 8);
    icmp6_msg[5] = (u8) (identifier & 0xFF);
    icmp6_msg[6] = (u8) (sequence >> 8);
    icmp6_msg[7] = (u8) (sequence & 0xFF);
    icmp6_msg[8] = (u8) 'p';
    icmp6_msg[9] = (u8) 'i';
    icmp6_msg[10] = (u8) 'n';
    icmp6_msg[11] = (u8) 'g';
    u16 csum = ipv6_upper_layer_checksum(src_ip6, target_ip6, IP_PROTOCOL_ICMPV6, &icmp6_msg[0], 12);
    icmp6_msg[2] = (u8) (csum >> 8);
    icmp6_msg[3] = (u8) (csum & 0xFF);

    // Ethernet (14) + IPv6 (40) + ICMPv6 (12) = 66 bytes.
    u8 frame[66];
    int i = 0;
    while (i < 6) {
        frame[i] = dest_mac[i];
        frame[6 + i] = src_mac[i];
        i = i + 1;
    }
    frame[12] = 0x86;   // EtherType = 0x86DD (IPv6)
    frame[13] = 0xDD;

    ipv6_build_header(&frame[14], src_ip6, target_ip6, IP_PROTOCOL_ICMPV6, 12);

    i = 0;
    while (i < 12) {
        frame[54 + i] = icmp6_msg[i];
        i = i + 1;
    }

    if (!e1000_send(&frame[0], 66)) {
        return false;
    }

    u8 reply[128];
    u64 start_tick = g_tick_count;
    while (g_tick_count - start_tick < 2000) {
        u16 len = e1000_receive(&reply[0], 128);
        if (len > 0) {
            bool is_ipv6 = reply[12] == 0x86 && reply[13] == 0xDD;
            if (is_ipv6) {
                u8 next_header = reply[14 + 6];
                bool is_icmp6 = next_header == IP_PROTOCOL_ICMPV6;
                bool src_matches = ipv6_equals(&reply[14 + 8], target_ip6);
                u8 icmp6_type = reply[14 + 40];
                bool is_echo_reply = icmp6_type == ICMPV6_ECHO_REPLY;
                u16 reply_id = (((u16) reply[14 + 40 + 4]) << 8) | ((u16) reply[14 + 40 + 5]);
                u16 reply_seq = (((u16) reply[14 + 40 + 6]) << 8) | ((u16) reply[14 + 40 + 7]);
                if (is_icmp6 && src_matches && is_echo_reply
                    && reply_id == identifier && reply_seq == sequence) {
                    return true;
                }
            }
        }
        yield();  // this runs on a background worker task now
    }
    return false;
}
