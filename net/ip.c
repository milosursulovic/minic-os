// IPv4 layer: header construction, RFC 791 checksum, and g_my_ip - still
// static/hardcoded (QEMU SLIRP's default guest address), not DHCP-obtained.

#include "ip.h"

u8 g_my_ip[4];

void ip_init(void) {
    g_my_ip[0] = 10;
    g_my_ip[1] = 0;
    g_my_ip[2] = 2;
    g_my_ip[3] = 15;
}

// Sum 16-bit words (odd trailing byte zero-padded), fold carry, one's complement.
u16 ip_checksum(u8* data, u32 len) {
    u32 sum = 0;
    u32 i = 0;
    while (i + 1 < len) {
        u32 word = (((u32) data[i]) << 8) | ((u32) data[i + 1]);
        sum = sum + word;
        i = i + 2;
    }
    if (i < len) {
        sum = sum + (((u32) data[i]) << 8);
    }
    while ((sum >> 16) != 0) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return (u16) (~sum & 0xFFFF);
}

// Builds a 20-byte IPv4 header (no options, unfragmented) at out[0..19].
void ip_build_header(u8* out, u8* src_ip, u8* dst_ip, u8 protocol, u16 payload_len) {
    out[0] = 0x45;   // version 4, IHL 5 (20 bytes, no options)
    out[1] = 0x00;   // type of service
    u16 total_len = 20 + payload_len;
    out[2] = (u8) (total_len >> 8);
    out[3] = (u8) (total_len & 0xFF);
    out[4] = 0x00;   // identification
    out[5] = 0x00;
    out[6] = 0x40;   // flags = don't fragment, fragment offset = 0
    out[7] = 0x00;
    out[8] = 64;     // TTL
    out[9] = protocol;
    out[10] = 0;     // checksum placeholder - filled in below
    out[11] = 0;
    int i = 0;
    while (i < 4) {
        out[12 + i] = src_ip[i];
        out[16 + i] = dst_ip[i];
        i = i + 1;
    }
    u16 csum = ip_checksum(out, 20);
    out[10] = (u8) (csum >> 8);
    out[11] = (u8) (csum & 0xFF);
}
