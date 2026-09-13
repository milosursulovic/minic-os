// See ipv6.h's own comment for scope.

#include "ipv6.h"
#include "../ip/ip.h"
#include "../e1000/e1000.h"

u8 g_my_ipv6_link_local[16];
u8 g_my_ipv6_global[16];
bool g_ipv6_global_valid;
u8 g_ipv6_default_router[16];

static bool g_ipv6_link_local_ready;

bool ipv6_equals(u8* a, u8* b) {
    int i = 0;
    while (i < 16) {
        if (a[i] != b[i]) {
            return false;
        }
        i = i + 1;
    }
    return true;
}

// RFC 4291 appendix A: split the 6-byte MAC around the middle, insert
// ff:fe, flip the universal/local bit (bit 1 of the first byte) - the
// standard modified-EUI-64 interface identifier.
static void mac_to_eui64(u8* mac, u8* out) {
    out[0] = mac[0] ^ 0x02;
    out[1] = mac[1];
    out[2] = mac[2];
    out[3] = 0xFF;
    out[4] = 0xFE;
    out[5] = mac[3];
    out[6] = mac[4];
    out[7] = mac[5];
}

void ipv6_init_link_local(void) {
    if (g_ipv6_link_local_ready) {
        return;
    }
    u8 mac[6];
    e1000_get_mac(&mac[0]);
    u8 eui64[8];
    mac_to_eui64(&mac[0], &eui64[0]);

    g_my_ipv6_link_local[0] = 0xFE;
    g_my_ipv6_link_local[1] = 0x80;
    int i = 2;
    while (i < 8) {
        g_my_ipv6_link_local[i] = 0;
        i = i + 1;
    }
    i = 0;
    while (i < 8) {
        g_my_ipv6_link_local[8 + i] = eui64[i];
        i = i + 1;
    }
    g_ipv6_link_local_ready = true;
}

// Combines an advertised prefix (RA's Prefix Information option) with our
// own EUI-64 interface ID - the real RFC 4862 SLAAC mechanism. Exposed
// for kernel/net/ndp/ndp.c's ndp_do_slaac() to call once it has a real RA.
void ipv6_build_slaac_address(u8* prefix, u8 prefix_len, u8* out) {
    (void) prefix_len;  // this kernel only ever forms /64 SLAAC addresses - the only prefix length RFC 4862 SLAAC actually supports
    u8 mac[6];
    e1000_get_mac(&mac[0]);
    u8 eui64[8];
    mac_to_eui64(&mac[0], &eui64[0]);
    int i = 0;
    while (i < 8) {
        out[i] = prefix[i];
        i = i + 1;
    }
    i = 0;
    while (i < 8) {
        out[8 + i] = eui64[i];
        i = i + 1;
    }
}

void ipv6_build_header(u8* out, u8* src_ip6, u8* dst_ip6, u8 next_header, u16 payload_len) {
    out[0] = 0x60;  // version 6, traffic class high nibble = 0
    out[1] = 0x00;  // traffic class low nibble + flow label high nibble
    out[2] = 0x00;  // flow label
    out[3] = 0x00;
    out[4] = (u8) (payload_len >> 8);
    out[5] = (u8) (payload_len & 0xFF);
    out[6] = next_header;
    out[7] = 64;    // hop limit - same value kernel/net/ip/ip.c's IPv4 TTL already uses
    int i = 0;
    while (i < 16) {
        out[8 + i] = src_ip6[i];
        out[24 + i] = dst_ip6[i];
        i = i + 1;
    }
}

u16 ipv6_upper_layer_checksum(u8* src_ip6, u8* dst_ip6, u8 next_header, u8* segment, u16 segment_len) {
    u8 buf[1500];
    int i = 0;
    while (i < 16) {
        buf[i] = src_ip6[i];
        buf[16 + i] = dst_ip6[i];
        i = i + 1;
    }
    u32 len32 = segment_len;
    buf[32] = (u8) (len32 >> 24);
    buf[33] = (u8) (len32 >> 16);
    buf[34] = (u8) (len32 >> 8);
    buf[35] = (u8) (len32 & 0xFF);
    buf[36] = 0;
    buf[37] = 0;
    buf[38] = 0;
    buf[39] = next_header;
    i = 0;
    while (i < (int) segment_len) {
        buf[40 + i] = segment[i];
        i = i + 1;
    }
    u32 total_len = 40 + (u32) segment_len;
    if ((segment_len & 1) != 0) {
        buf[40 + segment_len] = 0;
        total_len = total_len + 1;
    }
    return ip_checksum(&buf[0], total_len);
}

void ipv6_solicited_node_multicast(u8* target_ip6, u8* out) {
    out[0] = 0xFF;
    out[1] = 0x02;
    int i = 2;
    while (i < 11) {
        out[i] = 0;
        i = i + 1;
    }
    out[11] = 0x01;
    out[12] = 0xFF;
    out[13] = target_ip6[13];
    out[14] = target_ip6[14];
    out[15] = target_ip6[15];
}

void ipv6_multicast_mac(u8* ip6_multicast_addr, u8* mac_out) {
    mac_out[0] = 0x33;
    mac_out[1] = 0x33;
    mac_out[2] = ip6_multicast_addr[12];
    mac_out[3] = ip6_multicast_addr[13];
    mac_out[4] = ip6_multicast_addr[14];
    mac_out[5] = ip6_multicast_addr[15];
}
