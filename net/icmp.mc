// Milestone 33 (Phase X's fifth step): ICMP echo (ping) - the minimal,
// natural verification vehicle for "does the IP layer genuinely work,"
// the same relationship milestone 31's hardcoded ARP request had to
// proving TX/RX worked. A bare IP packet with no upper-layer protocol
// has nothing that could reply to it; ICMP echo is the smallest real
// protocol that gets an external, independently-meaningful response.
//
// Deliberately just echo request/reply - no other ICMP message types
// (destination unreachable, time exceeded, etc.), the same "narrowest
// safe first version" every driver/protocol-layer milestone in this
// phase has used.

import "ip.mc";
import "arp.mc";
import "e1000.mc";
import "../isr/isr.mc";

const u8 ICMP_ECHO_REQUEST = 8;
const u8 ICMP_ECHO_REPLY = 0;
const u8 IP_PROTOCOL_ICMP = 1;

// Resolves targetIp's MAC (reusing milestone 32's real resolver - a
// cache hit here costs nothing), builds a real Ethernet+IPv4+ICMP echo
// request, sends it, and polls (real gTickCount-bounded, not a raw spin
// - milestone 31/32's already-learned lesson) for a reply that
// genuinely matches: right EtherType, right IP protocol, right source
// IP, right ICMP type, AND the exact identifier/sequence this request
// sent - not just "something ICMP arrived." Returns false (a real,
// honest failure) if ARP resolution fails or nothing matching arrives
// within the timeout.
bool icmpPing(u8* targetIp, u16 identifier, u16 sequence) {
    u8 destMac[6];
    if (!arpResolve(targetIp, &destMac[0])) {
        return false;
    }
    ipInit();
    u8 srcMac[6];
    e1000GetMac(&srcMac[0]);

    // 8-byte ICMP header + a small, fixed 4-byte payload.
    u8 icmpMsg[12];
    icmpMsg[0] = ICMP_ECHO_REQUEST;
    icmpMsg[1] = 0;
    icmpMsg[2] = 0;   // checksum placeholder
    icmpMsg[3] = 0;
    icmpMsg[4] = (u8) (identifier >> 8);
    icmpMsg[5] = (u8) (identifier & 0xFF);
    icmpMsg[6] = (u8) (sequence >> 8);
    icmpMsg[7] = (u8) (sequence & 0xFF);
    icmpMsg[8] = (u8) 'p';
    icmpMsg[9] = (u8) 'i';
    icmpMsg[10] = (u8) 'n';
    icmpMsg[11] = (u8) 'g';
    u16 icmpCsum = ipChecksum(&icmpMsg[0], 12);
    icmpMsg[2] = (u8) (icmpCsum >> 8);
    icmpMsg[3] = (u8) (icmpCsum & 0xFF);

    // Ethernet (14) + IPv4 (20) + ICMP (12) = 46 bytes.
    u8 frame[64];
    int i = 0;
    while (i < 64) {
        frame[i] = 0;
        i = i + 1;
    }
    i = 0;
    while (i < 6) {
        frame[i] = destMac[i];
        i = i + 1;
    }
    i = 0;
    while (i < 6) {
        frame[6 + i] = srcMac[i];
        i = i + 1;
    }
    frame[12] = 0x08;   // EtherType = 0x0800 (IPv4)
    frame[13] = 0x00;

    ipBuildHeader(&frame[14], &gMyIp[0], targetIp, IP_PROTOCOL_ICMP, 12);

    i = 0;
    while (i < 12) {
        frame[34 + i] = icmpMsg[i];
        i = i + 1;
    }

    if (!e1000Send(&frame[0], 46)) {
        return false;
    }

    u8 reply[128];
    u64 startTick = gTickCount;
    while (gTickCount - startTick < 2000) {
        u16 len = e1000Receive(&reply[0], 128);
        if (len > 0) {
            bool isIp = reply[12] == 0x08 && reply[13] == 0x00;
            if (isIp) {
                // IP header assumed IHL=5 (20 bytes, no options) - a
                // real assumption, safe here since nothing this kernel
                // ever sends includes options, so nothing it's talking
                // to has a reason to reply with any either.
                u8 proto = reply[14 + 9];
                bool isIcmp = proto == IP_PROTOCOL_ICMP;
                bool srcMatches = reply[14 + 12] == targetIp[0]
                    && reply[14 + 13] == targetIp[1]
                    && reply[14 + 14] == targetIp[2]
                    && reply[14 + 15] == targetIp[3];
                u8 icmpType = reply[34];
                bool isEchoReply = icmpType == ICMP_ECHO_REPLY;
                u16 replyId = (((u16) reply[38]) << 8) | ((u16) reply[39]);
                u16 replySeq = (((u16) reply[40]) << 8) | ((u16) reply[41]);
                if (isIcmp && srcMatches && isEchoReply
                    && replyId == identifier && replySeq == sequence) {
                    return true;
                }
            }
            // Something else arrived (not a matching echo reply) -
            // ignore it and keep polling within the remaining budget.
        }
    }
    return false;
}
