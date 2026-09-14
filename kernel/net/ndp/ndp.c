// See ndp.h's own comment for scope. Same overall shape as
// kernel/net/arp/arp.c throughout - a fixed-size cache, no eviction/TTL,
// tick-bounded (not count-bounded) polling.

#include "ndp.h"
#include "../ipv6/ipv6.h"
#include "../arp/arp.h"
#include "../netdev/netdev.h"
#include "../../isr/isr.h"
#include "../../sched/task.h"

static const u8 IP_PROTOCOL_ICMPV6 = 58;

static const u8 ICMPV6_TYPE_RS = 133;
static const u8 ICMPV6_TYPE_RA = 134;
static const u8 ICMPV6_TYPE_NS = 135;
static const u8 ICMPV6_TYPE_NA = 136;

static const u8 ICMPV6_OPT_SOURCE_LINK_ADDR = 1;
static const u8 ICMPV6_OPT_TARGET_LINK_ADDR = 2;
static const u8 ICMPV6_OPT_PREFIX_INFO = 3;

typedef struct {
    bool used;
    u8 ip6[16];
    u8 mac[6];
} ndp_entry;

#define NDP_CACHE_SIZE 8
static ndp_entry g_ndp_cache[NDP_CACHE_SIZE];
static u32 g_ndp_cache_count;

static bool ndp_cache_lookup(u8* ip6, u8* mac_out) {
    u32 i = 0;
    while (i < g_ndp_cache_count) {
        if (g_ndp_cache[i].used && ipv6_equals(&g_ndp_cache[i].ip6[0], ip6)) {
            u32 j = 0;
            while (j < 6) {
                mac_out[j] = g_ndp_cache[i].mac[j];
                j = j + 1;
            }
            return true;
        }
        i = i + 1;
    }
    return false;
}

static void ndp_cache_insert(u8* ip6, u8* mac) {
    if (g_ndp_cache_count >= NDP_CACHE_SIZE) {
        return;
    }
    u32 slot = g_ndp_cache_count;
    g_ndp_cache[slot].used = true;
    u32 j = 0;
    while (j < 16) {
        g_ndp_cache[slot].ip6[j] = ip6[j];
        j = j + 1;
    }
    j = 0;
    while (j < 6) {
        g_ndp_cache[slot].mac[j] = mac[j];
        j = j + 1;
    }
    g_ndp_cache_count = g_ndp_cache_count + 1;
}

static void all_routers_multicast(u8* out) {
    out[0] = 0xFF;
    out[1] = 0x02;
    int i = 2;
    while (i < 15) {
        out[i] = 0;
        i = i + 1;
    }
    out[15] = 0x02;
}

// Builds and sends one Ethernet+IPv6+ICMPv6 Neighbor Solicitation for
// target_ip6, to its solicited-node multicast address.
static void ndp_send_neighbor_solicitation(u8* target_ip6) {
    ipv6_init_link_local();
    u8 mac[6];
    netdev_get_mac(&mac[0]);

    u8 sn_multicast[16];
    ipv6_solicited_node_multicast(target_ip6, &sn_multicast[0]);
    u8 dest_mac[6];
    ipv6_multicast_mac(&sn_multicast[0], &dest_mac[0]);

    u8 icmp6_msg[32];
    icmp6_msg[0] = ICMPV6_TYPE_NS;
    icmp6_msg[1] = 0;
    icmp6_msg[2] = 0;
    icmp6_msg[3] = 0;
    icmp6_msg[4] = 0;
    icmp6_msg[5] = 0;
    icmp6_msg[6] = 0;
    icmp6_msg[7] = 0;
    int i = 0;
    while (i < 16) {
        icmp6_msg[8 + i] = target_ip6[i];
        i = i + 1;
    }
    icmp6_msg[24] = ICMPV6_OPT_SOURCE_LINK_ADDR;
    icmp6_msg[25] = 1;  // option length in 8-byte units
    i = 0;
    while (i < 6) {
        icmp6_msg[26 + i] = mac[i];
        i = i + 1;
    }
    u16 csum = ipv6_upper_layer_checksum(&g_my_ipv6_link_local[0], &sn_multicast[0],
                                          IP_PROTOCOL_ICMPV6, &icmp6_msg[0], 32);
    icmp6_msg[2] = (u8) (csum >> 8);
    icmp6_msg[3] = (u8) (csum & 0xFF);

    u8 frame[14 + 40 + 32];
    i = 0;
    while (i < 6) {
        frame[i] = dest_mac[i];
        frame[6 + i] = mac[i];
        i = i + 1;
    }
    frame[12] = 0x86;
    frame[13] = 0xDD;
    ipv6_build_header(&frame[14], &g_my_ipv6_link_local[0], &sn_multicast[0], IP_PROTOCOL_ICMPV6, 32);
    i = 0;
    while (i < 32) {
        frame[54 + i] = icmp6_msg[i];
        i = i + 1;
    }
    netdev_send(&frame[0], 14 + 40 + 32);
}

static const u64 NDP_TIMEOUT_TICKS = 2000;

static bool ndp_wait_neighbor_advertisement(u8* target_ip6, u64 timeout_ticks, u8* mac_out) {
    u8 buf[128];
    u64 start_tick = g_tick_count;
    while (g_tick_count - start_tick < timeout_ticks) {
        yield();
        u16 len = netdev_receive(&buf[0], 128);
        if (len == 0) {
            continue;
        }
        bool is_ipv6 = buf[12] == 0x86 && buf[13] == 0xDD;
        if (!is_ipv6) {
            continue;
        }
        u8 next_header = buf[14 + 6];
        if (next_header != IP_PROTOCOL_ICMPV6) {
            continue;
        }
        u8 icmp6_type = buf[14 + 40];
        if (icmp6_type != ICMPV6_TYPE_NA) {
            continue;
        }
        u8* na_target = &buf[14 + 40 + 8];  // past the 4-byte header + 4-byte flags field
        if (!ipv6_equals(na_target, target_ip6)) {
            continue;
        }
        u8* opt = &buf[14 + 40 + 8 + 16];
        if (opt[0] != ICMPV6_OPT_TARGET_LINK_ADDR) {
            continue;
        }
        int i = 0;
        while (i < 6) {
            mac_out[i] = opt[2 + i];
            i = i + 1;
        }
        return true;
    }
    return false;
}

bool ndp_resolve(u8* target_ip6, u8* mac_out) {
    if (ndp_cache_lookup(target_ip6, mac_out)) {
        return true;
    }
    if (!arp_init()) {
        return false;
    }
    ndp_send_neighbor_solicitation(target_ip6);
    if (ndp_wait_neighbor_advertisement(target_ip6, NDP_TIMEOUT_TICKS, mac_out)) {
        ndp_cache_insert(target_ip6, mac_out);
        return true;
    }
    return false;
}

static void ndp_send_router_solicitation(void) {
    ipv6_init_link_local();
    u8 mac[6];
    netdev_get_mac(&mac[0]);

    u8 all_routers[16];
    all_routers_multicast(&all_routers[0]);
    u8 dest_mac[6];
    ipv6_multicast_mac(&all_routers[0], &dest_mac[0]);

    u8 icmp6_msg[16];
    icmp6_msg[0] = ICMPV6_TYPE_RS;
    icmp6_msg[1] = 0;
    icmp6_msg[2] = 0;
    icmp6_msg[3] = 0;
    icmp6_msg[4] = 0;
    icmp6_msg[5] = 0;
    icmp6_msg[6] = 0;
    icmp6_msg[7] = 0;
    icmp6_msg[8] = ICMPV6_OPT_SOURCE_LINK_ADDR;
    icmp6_msg[9] = 1;
    int i = 0;
    while (i < 6) {
        icmp6_msg[10 + i] = mac[i];
        i = i + 1;
    }
    u16 csum = ipv6_upper_layer_checksum(&g_my_ipv6_link_local[0], &all_routers[0],
                                          IP_PROTOCOL_ICMPV6, &icmp6_msg[0], 16);
    icmp6_msg[2] = (u8) (csum >> 8);
    icmp6_msg[3] = (u8) (csum & 0xFF);

    u8 frame[14 + 40 + 16];
    i = 0;
    while (i < 6) {
        frame[i] = dest_mac[i];
        frame[6 + i] = mac[i];
        i = i + 1;
    }
    frame[12] = 0x86;
    frame[13] = 0xDD;
    ipv6_build_header(&frame[14], &g_my_ipv6_link_local[0], &all_routers[0], IP_PROTOCOL_ICMPV6, 16);
    i = 0;
    while (i < 16) {
        frame[54 + i] = icmp6_msg[i];
        i = i + 1;
    }
    netdev_send(&frame[0], 14 + 40 + 16);
}

// Waits for a real Router Advertisement carrying a Prefix Information
// option (type 3) - a RA with no such option (e.g. a bare default-route
// announcement) doesn't satisfy SLAAC, so polling continues past it.
static bool ndp_wait_router_advertisement(u64 timeout_ticks, u8* prefix_out, u8* prefix_len_out, u8* router_ip6_out) {
    u8 buf[300];
    u64 start_tick = g_tick_count;
    while (g_tick_count - start_tick < timeout_ticks) {
        yield();
        u16 len = netdev_receive(&buf[0], 300);
        if (len == 0) {
            continue;
        }
        bool is_ipv6 = buf[12] == 0x86 && buf[13] == 0xDD;
        if (!is_ipv6) {
            continue;
        }
        u8 next_header = buf[14 + 6];
        if (next_header != IP_PROTOCOL_ICMPV6) {
            continue;
        }
        u8 icmp6_type = buf[14 + 40];
        if (icmp6_type != ICMPV6_TYPE_RA) {
            continue;
        }

        int i = 0;
        while (i < 16) {
            router_ip6_out[i] = buf[14 + 8 + i];  // IPv6 header's own source address
            i = i + 1;
        }

        u16 ip6_payload_len = (((u16) buf[14 + 4]) << 8) | ((u16) buf[14 + 5]);
        u32 icmp6_offset = 14 + 40;
        u32 opt_offset = icmp6_offset + 16;  // fixed RA header: 4(icmp)+1+1+2+4+4=16
        u32 msg_end = icmp6_offset + ip6_payload_len;
        bool found_prefix = false;
        while (opt_offset + 1 < msg_end && opt_offset + 1 < (u32) len) {
            u8 opt_type = buf[opt_offset];
            u8 opt_len_units = buf[opt_offset + 1];
            if (opt_len_units == 0) {
                break;  // malformed - avoid an infinite loop
            }
            u32 opt_len_bytes = (u32) opt_len_units * 8;
            if (opt_type == ICMPV6_OPT_PREFIX_INFO && opt_len_bytes >= 32) {
                u8 prefix_length = buf[opt_offset + 2];
                i = 0;
                while (i < 16) {
                    prefix_out[i] = buf[opt_offset + 16 + i];
                    i = i + 1;
                }
                *prefix_len_out = prefix_length;
                found_prefix = true;
            }
            opt_offset = opt_offset + opt_len_bytes;
        }
        if (found_prefix) {
            return true;
        }
    }
    return false;
}

#define NDP_RS_RTO_TICKS 300
#define NDP_RS_MAX_ATTEMPTS 4

bool ndp_do_slaac(void) {
    ipv6_init_link_local();
    if (!arp_init()) {
        return false;
    }
    u8 prefix[16];
    u8 prefix_len = 0;
    u8 router_ip6[16];
    int attempt = 0;
    while (attempt < NDP_RS_MAX_ATTEMPTS) {
        ndp_send_router_solicitation();
        if (ndp_wait_router_advertisement(NDP_RS_RTO_TICKS, &prefix[0], &prefix_len, &router_ip6[0])) {
            ipv6_build_slaac_address(&prefix[0], prefix_len, &g_my_ipv6_global[0]);
            g_ipv6_global_valid = true;
            int i = 0;
            while (i < 16) {
                g_ipv6_default_router[i] = router_ip6[i];
                i = i + 1;
            }
            return true;
        }
        attempt = attempt + 1;
    }
    return false;
}
