// Milestone 34 (Phase X's sixth step): a real UDP layer - the simpler
// of the two transport protocols (no connection state, no
// retransmission/congestion control, just a port-addressed datagram
// wrapper over milestone 33's now-working IP layer). TCP's real
// connection state machine, sequence numbers, and retransmission are a
// substantially bigger, genuinely separate hard problem, deliberately
// left for a later milestone - the same "narrowest safe first version"
// discipline every protocol-layer milestone in this phase has used.

import "ip.mc";
import "arp.mc";
import "e1000.mc";
import "../isr/isr.mc";

const u8 IP_PROTOCOL_UDP = 17;

// The real UDP checksum: unlike IP's own header checksum (which covers
// only the 20 header bytes), UDP's checksum covers a 12-byte
// "pseudo-header" (source IP, dest IP, a zero byte, the protocol
// number, and the UDP length) PLUS the real UDP header and payload -
// binding the checksum to the addresses it's actually being delivered
// between, not just the datagram's own bytes. Reuses ip.mc's
// ipChecksum() algorithm unchanged (the same one's-complement sum
// covers both) by assembling the pseudo-header + message into one
// scratch buffer first.
u16 udpChecksum(u8* srcIp, u8* dstIp, u8* udpMsg, u16 udpLen) {
    u8 buf[128];
    int i = 0;
    while (i < 4) {
        buf[i] = srcIp[i];
        buf[4 + i] = dstIp[i];
        i = i + 1;
    }
    buf[8] = 0;
    buf[9] = IP_PROTOCOL_UDP;
    buf[10] = (u8) (udpLen >> 8);
    buf[11] = (u8) (udpLen & 0xFF);
    i = 0;
    while (i < (int) udpLen) {
        buf[12 + i] = udpMsg[i];
        i = i + 1;
    }
    u32 totalLen = 12 + (u32) udpLen;
    if ((udpLen & 1) != 0) {
        buf[12 + udpLen] = 0;
        totalLen = totalLen + 1;
    }
    return ipChecksum(&buf[0], totalLen);
}

// Builds an 8-byte UDP header at out[0..7] - the caller must have
// already placed the real payload at out[8..8+payloadLen) before
// calling this, since the checksum has to cover it.
void udpBuildHeader(u8* out, u16 srcPort, u16 dstPort, u16 payloadLen, u8* srcIp, u8* dstIp) {
    u16 udpLen = 8 + payloadLen;
    out[0] = (u8) (srcPort >> 8);
    out[1] = (u8) (srcPort & 0xFF);
    out[2] = (u8) (dstPort >> 8);
    out[3] = (u8) (dstPort & 0xFF);
    out[4] = (u8) (udpLen >> 8);
    out[5] = (u8) (udpLen & 0xFF);
    out[6] = 0;
    out[7] = 0;
    u16 csum = udpChecksum(srcIp, dstIp, out, udpLen);
    out[6] = (u8) (csum >> 8);
    out[7] = (u8) (csum & 0xFF);
}

// Resolves the target's MAC (milestone 32's real arpResolve()), builds
// a real Ethernet+IPv4+UDP datagram carrying `payload`, and sends it.
// Real hardware confirmation of the send comes from e1000Send()'s own
// descriptor-done check, same as every other protocol built on top of
// it so far.
bool udpSend(u8* targetIp, u16 dstPort, u16 srcPort, u8* payload, u16 payloadLen) {
    u8 destMac[6];
    if (!arpResolve(targetIp, &destMac[0])) {
        return false;
    }
    ipInit();
    u8 srcMac[6];
    e1000GetMac(&srcMac[0]);

    u8 frame[128];
    int i = 0;
    while (i < 128) {
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
    frame[12] = 0x08;
    frame[13] = 0x00;

    ipBuildHeader(&frame[14], &gMyIp[0], targetIp, IP_PROTOCOL_UDP, 8 + payloadLen);

    i = 0;
    while (i < (int) payloadLen) {
        frame[34 + 8 + i] = payload[i];
        i = i + 1;
    }
    udpBuildHeader(&frame[34], srcPort, dstPort, payloadLen, &gMyIp[0], targetIp);

    u16 frameLen = 42 + payloadLen;
    return e1000Send(&frame[0], frameLen);
}

// Polls (real gTickCount-bounded, milestone 31/32/33's already-learned
// timing lesson) for a UDP datagram genuinely matching every field that
// matters: right EtherType, right IP protocol, right source IP, right
// source AND destination port. Copies the real payload into `out` and
// returns its length, or 0 (a real, honest "nothing matching arrived")
// if the timeout expires.
u16 udpReceive(u8* expectedSrcIp, u16 expectedSrcPort, u16 expectedDstPort, u8* out, u16 maxLen) {
    u8 reply[160];
    u64 startTick = gTickCount;
    while (gTickCount - startTick < 2000) {
        u16 len = e1000Receive(&reply[0], 160);
        if (len > 0) {
            bool isIp = reply[12] == 0x08 && reply[13] == 0x00;
            if (isIp) {
                u8 proto = reply[14 + 9];
                bool isUdp = proto == IP_PROTOCOL_UDP;
                bool srcMatches = reply[14 + 12] == expectedSrcIp[0]
                    && reply[14 + 13] == expectedSrcIp[1]
                    && reply[14 + 14] == expectedSrcIp[2]
                    && reply[14 + 15] == expectedSrcIp[3];
                u16 srcPort = (((u16) reply[34]) << 8) | ((u16) reply[35]);
                u16 dstPort = (((u16) reply[36]) << 8) | ((u16) reply[37]);
                if (isUdp && srcMatches && srcPort == expectedSrcPort && dstPort == expectedDstPort) {
                    u16 udpLen = (((u16) reply[38]) << 8) | ((u16) reply[39]);
                    u16 payloadLen = udpLen - 8;
                    u16 copyLen = payloadLen;
                    if (copyLen > maxLen) {
                        copyLen = maxLen;
                    }
                    int j = 0;
                    while (j < (int) copyLen) {
                        out[j] = reply[42 + j];
                        j = j + 1;
                    }
                    return payloadLen;
                }
            }
            // Something else arrived (not a matching datagram) - ignore
            // it and keep polling within the remaining budget.
        }
    }
    return 0;
}
