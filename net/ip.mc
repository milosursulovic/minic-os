// Milestone 33 (Phase X's fifth step): a real IPv4 layer - header
// construction, the real one's-complement checksum algorithm (RFC 791),
// and this kernel's own long-overdue concept of "my IP address."
//
// `gMyIp` used to be a hardcoded literal duplicated wherever a sender IP
// was needed (net/arp.mc's own arpSendRequest()) - now one real, named,
// shared value. Still deliberately static/hardcoded, not DHCP-obtained:
// this kernel has no real IP configuration mechanism at all yet (see
// Known limitations) - matches QEMU SLIRP's own conventional default
// guest address, the same value arp.mc's sender-IP field already
// assumed before this milestone gave it a real name.

u8 gMyIp[4];

void ipInit() {
    gMyIp[0] = 10;
    gMyIp[1] = 0;
    gMyIp[2] = 2;
    gMyIp[3] = 15;
}

// The standard IPv4/ICMP checksum: sum every 16-bit word (a trailing odd
// byte is padded with a zero low byte), fold any carry back into the
// low 16 bits, then take the one's complement. The same algorithm
// covers both an IP header and an ICMP message - only the byte range
// checksummed differs.
u16 ipChecksum(u8* data, u32 len) {
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

// Builds a 20-byte IPv4 header (no options - IHL=5) at out[0..19].
// Version/IHL, TOS, total length, ID/flags/fragment-offset are all
// fixed/simple choices appropriate for a single, unfragmented packet -
// this kernel never needs to fragment anything it sends, since every
// payload it builds today fits in one frame well under the Ethernet MTU.
void ipBuildHeader(u8* out, u8* srcIp, u8* dstIp, u8 protocol, u16 payloadLen) {
    out[0] = 0x45;   // version 4, IHL 5 (20 bytes, no options)
    out[1] = 0x00;   // type of service
    u16 totalLen = 20 + payloadLen;
    out[2] = (u8) (totalLen >> 8);
    out[3] = (u8) (totalLen & 0xFF);
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
        out[12 + i] = srcIp[i];
        out[16 + i] = dstIp[i];
        i = i + 1;
    }
    u16 csum = ipChecksum(out, 20);
    out[10] = (u8) (csum >> 8);
    out[11] = (u8) (csum & 0xFF);
}
