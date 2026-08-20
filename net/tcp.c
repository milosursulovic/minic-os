// Milestone 35, Phase X's seventh step: a real TCP client - the first
// genuinely stateful transport protocol in this kernel, and the first
// time any protocol layer here talks to a host that ISN'T on the local
// SLIRP subnet (the gateway at 10.0.2.2, the DNS proxy at 10.0.2.3).
// That matters at the framing level: a real internet host is reached by
// sending the Ethernet frame to the GATEWAY's MAC address (ARP-resolved,
// same as every earlier protocol layer) while the IP header's own
// destination address is the real remote host - routing, not resolution,
// decides where an off-subnet frame's link-layer destination is, and
// this is the first file in this kernel that has to make that
// distinction instead of ARPing the real destination directly (which
// would just time out - nothing off-subnet ever answers an ARP request
// from this guest).
//
// Deliberately scoped as a CLIENT only, one connection at a time, no
// connection table, no listening/server side, no retransmission or
// congestion control (a lost segment just times out, the same "narrowest
// safe first version" every earlier protocol-layer milestone in this
// phase used) - real handshake, real data exchange, and a best-effort
// graceful close is the whole scope. Retransmission/congestion control
// is real, substantially separate follow-on work, not attempted here.

#include "tcp.h"
#include "ip.h"
#include "arp.h"
#include "e1000.h"
#include "dns.h"
#include "../isr/isr.h"

static const u8 IP_PROTOCOL_TCP = 6;

static const u8 TCP_FLAG_FIN = 0x01;
static const u8 TCP_FLAG_SYN = 0x02;
static const u8 TCP_FLAG_RST = 0x04;
static const u8 TCP_FLAG_PSH = 0x08;
static const u8 TCP_FLAG_ACK = 0x10;

// Real internet hosts sit off the 10.0.2.0/24 SLIRP subnet - every frame
// bound for one still has to leave through the gateway at the link
// layer, same as any real router hop.
static const u8 GATEWAY_IP[4] = {10, 0, 2, 2};

// The real TCP checksum: a 12-byte pseudo-header (source IP, dest IP, a
// zero byte, protocol number 6, and the TCP segment length) PLUS the
// real TCP header+payload - the exact same shape udp.c's own
// udp_checksum() already established for UDP's pseudo-header, just a
// different protocol number and covering a variable-length segment
// instead of a fixed 8-byte header.
static u16 tcp_checksum(u8* src_ip, u8* dst_ip, u8* segment, u16 segment_len) {
    u8 buf[1500];
    int i = 0;
    while (i < 4) {
        buf[i] = src_ip[i];
        buf[4 + i] = dst_ip[i];
        i = i + 1;
    }
    buf[8] = 0;
    buf[9] = IP_PROTOCOL_TCP;
    buf[10] = (u8) (segment_len >> 8);
    buf[11] = (u8) (segment_len & 0xFF);
    i = 0;
    while (i < (int) segment_len) {
        buf[12 + i] = segment[i];
        i = i + 1;
    }
    u32 total_len = 12 + (u32) segment_len;
    if ((segment_len & 1) != 0) {
        buf[12 + segment_len] = 0;
        total_len = total_len + 1;
    }
    return ip_checksum(&buf[0], total_len);
}

// Builds a 20-byte TCP header (no options) at out[0..19], optionally
// followed by `payload` (already placed by the caller at out[20..]) -
// the checksum has to cover both, so payload_len is passed in rather
// than the caller filling the checksum field itself.
static void tcp_build_header(u8* out, u16 src_port, u16 dst_port, u32 seq, u32 ack,
                              u8 flags, u16 window, u8* src_ip, u8* dst_ip, u16 payload_len) {
    out[0] = (u8) (src_port >> 8);
    out[1] = (u8) (src_port & 0xFF);
    out[2] = (u8) (dst_port >> 8);
    out[3] = (u8) (dst_port & 0xFF);
    out[4] = (u8) (seq >> 24);
    out[5] = (u8) (seq >> 16);
    out[6] = (u8) (seq >> 8);
    out[7] = (u8) (seq & 0xFF);
    out[8] = (u8) (ack >> 24);
    out[9] = (u8) (ack >> 16);
    out[10] = (u8) (ack >> 8);
    out[11] = (u8) (ack & 0xFF);
    out[12] = 0x50;   // data offset = 5 (20 bytes, no options) in the top nibble
    out[13] = flags;
    out[14] = (u8) (window >> 8);
    out[15] = (u8) (window & 0xFF);
    out[16] = 0;      // checksum placeholder
    out[17] = 0;
    out[18] = 0;      // urgent pointer - unused, no URG flag ever set
    out[19] = 0;

    u16 segment_len = 20 + payload_len;
    u16 csum = tcp_checksum(src_ip, dst_ip, out, segment_len);
    out[16] = (u8) (csum >> 8);
    out[17] = (u8) (csum & 0xFF);
}

// A generous default window - this client never has more than one
// connection's worth of state in flight and never needs to throttle a
// real sender, so there's no real flow-control story to get right here.
static const u16 TCP_WINDOW = 8192;

static const u16 LOCAL_PORT = 43981;

// Sends one segment (SYN/ACK/FIN/PSH combinations, with or without a
// real payload) to target_ip:target_port over the gateway's own MAC -
// every send in this file goes through this one helper so the
// "off-subnet destination, on-subnet link-layer next hop" distinction
// only has to be handled correctly in one place.
static bool tcp_send_segment(u8* gateway_mac, u8* target_ip, u16 target_port,
                              u32 seq, u32 ack, u8 flags, u8* payload, u16 payload_len) {
    u8 frame[1500];
    int i = 0;
    while (i < 6) {
        frame[i] = gateway_mac[i];
        i = i + 1;
    }
    u8 src_mac[6];
    e1000_get_mac(&src_mac[0]);
    i = 0;
    while (i < 6) {
        frame[6 + i] = src_mac[i];
        i = i + 1;
    }
    frame[12] = 0x08;
    frame[13] = 0x00;

    ip_build_header(&frame[14], &g_my_ip[0], target_ip, IP_PROTOCOL_TCP, (u16) (20 + payload_len));

    i = 0;
    while (i < (int) payload_len) {
        frame[34 + 20 + i] = payload[i];
        i = i + 1;
    }
    tcp_build_header(&frame[34], LOCAL_PORT, target_port, seq, ack, flags, TCP_WINDOW,
                      &g_my_ip[0], target_ip, payload_len);

    u16 frame_len = (u16) (54 + payload_len);
    return e1000_send(&frame[0], frame_len);
}

// One received-segment's worth of parsed-out fields - every poll in
// this file wants the same handful of values, so pulling them out once
// keeps tcp_fetch()'s own state machine readable.
typedef struct {
    u32 seq;
    u32 ack;
    u8 flags;
    u16 payload_len;
    u16 payload_offset;   // into the caller's own receive buffer
} tcp_segment_info;

// Polls (real g_tick_count-bounded, the same timing discipline every
// protocol layer in this phase has used since milestone 31's own
// spin-vs-tick lesson) for a TCP segment genuinely from target_ip:
// target_port to this client's LOCAL_PORT. Ignores anything else -
// broadcast noise, unrelated traffic, stray retransmits from an earlier
// step - and keeps polling within the remaining budget.
static bool tcp_wait_segment(u8* target_ip, u16 target_port, u64 timeout_ticks,
                              u8* buf, u16 buf_len, tcp_segment_info* info_out) {
    u64 start_tick = g_tick_count;
    while (g_tick_count - start_tick < timeout_ticks) {
        u16 len = e1000_receive(buf, buf_len);
        if (len == 0) {
            continue;
        }
        bool is_ip = buf[12] == 0x08 && buf[13] == 0x00;
        if (!is_ip) {
            continue;
        }
        u8 proto = buf[14 + 9];
        if (proto != IP_PROTOCOL_TCP) {
            continue;
        }
        bool src_matches = buf[14 + 12] == target_ip[0] && buf[14 + 13] == target_ip[1]
            && buf[14 + 14] == target_ip[2] && buf[14 + 15] == target_ip[3];
        if (!src_matches) {
            continue;
        }
        u16 ip_total_len = (((u16) buf[16]) << 8) | ((u16) buf[17]);
        u16 src_port = (((u16) buf[34]) << 8) | ((u16) buf[35]);
        u16 dst_port = (((u16) buf[36]) << 8) | ((u16) buf[37]);
        if (src_port != target_port || dst_port != LOCAL_PORT) {
            continue;
        }
        u32 seq = (((u32) buf[38]) << 24) | (((u32) buf[39]) << 16) | (((u32) buf[40]) << 8) | ((u32) buf[41]);
        u32 ack = (((u32) buf[42]) << 24) | (((u32) buf[43]) << 16) | (((u32) buf[44]) << 8) | ((u32) buf[45]);
        u8 flags = buf[47];
        u16 tcp_header_len = (u16) ((buf[46] >> 4) * 4);
        u16 payload_len = (u16) (ip_total_len - 20 - tcp_header_len);

        info_out->seq = seq;
        info_out->ack = ack;
        info_out->flags = flags;
        info_out->payload_len = payload_len;
        info_out->payload_offset = (u16) (34 + tcp_header_len);
        return true;
    }
    return false;
}

bool tcp_fetch(u8* target_ip, u16 target_port, const char* request, u16 request_len,
               u8* response_out, u32 max_response_len, u32* response_len_out) {
    *response_len_out = 0;

    u8 gateway_mac[6];
    if (!arp_resolve((u8*) &GATEWAY_IP[0], &gateway_mac[0])) {
        return false;
    }
    ip_init();

    // A tick-derived initial sequence number - real TCP stacks use
    // something harder to predict for real security reasons, but
    // nothing in this client's own threat model depends on that; this
    // just needs to be *a* number, not the security-relevant kind.
    u32 my_seq = 0x10000 + (u32) g_tick_count;
    u32 peer_seq = 0;

    u8 recv_buf[1500];
    tcp_segment_info seg;

    // --- Three-way handshake ---
    if (!tcp_send_segment(&gateway_mac[0], target_ip, target_port, my_seq, 0, TCP_FLAG_SYN, NULL, 0)) {
        return false;
    }
    if (!tcp_wait_segment(target_ip, target_port, 3000, &recv_buf[0], 1500, &seg)) {
        return false;
    }
    if ((seg.flags & TCP_FLAG_RST) != 0) {
        return false;   // connection actively refused - a real, honest failure
    }
    if ((seg.flags & TCP_FLAG_SYN) == 0 || (seg.flags & TCP_FLAG_ACK) == 0 || seg.ack != my_seq + 1) {
        return false;   // not a genuine matching SYN-ACK
    }
    my_seq = my_seq + 1;
    peer_seq = seg.seq + 1;
    if (!tcp_send_segment(&gateway_mac[0], target_ip, target_port, my_seq, peer_seq, TCP_FLAG_ACK, NULL, 0)) {
        return false;
    }
    // Handshake complete - ESTABLISHED.

    // --- Send the real request as one data segment ---
    if (!tcp_send_segment(&gateway_mac[0], target_ip, target_port, my_seq, peer_seq,
                           TCP_FLAG_PSH | TCP_FLAG_ACK, (u8*) request, request_len)) {
        return false;
    }
    my_seq = my_seq + request_len;

    // --- Receive whatever real reply arrives, across as many segments
    // as show up within the budget, ACKing each - until the peer sends
    // a FIN (done replying) or the caller's buffer is full. ---
    bool peer_finished = false;
    u32 total_received = 0;
    u64 recv_deadline = g_tick_count + 3000;
    while (g_tick_count < recv_deadline && total_received < max_response_len) {
        if (!tcp_wait_segment(target_ip, target_port, recv_deadline - g_tick_count,
                               &recv_buf[0], 1500, &seg)) {
            break;   // timed out waiting for the next segment - stop with whatever arrived
        }
        if (seg.payload_len > 0) {
            u32 copy_len = seg.payload_len;
            if (total_received + copy_len > max_response_len) {
                copy_len = max_response_len - total_received;
            }
            u32 i = 0;
            while (i < copy_len) {
                response_out[total_received + i] = recv_buf[seg.payload_offset + i];
                i = i + 1;
            }
            total_received = total_received + copy_len;
            peer_seq = peer_seq + seg.payload_len;
        }
        if ((seg.flags & TCP_FLAG_FIN) != 0) {
            peer_seq = peer_seq + 1;
            peer_finished = true;
        }
        // Ack whatever we just processed (new data and/or a FIN both
        // consume sequence space that needs acknowledging) - real TCP
        // etiquette, and keeps peer_seq/my_seq honest for the close
        // below regardless of how this loop eventually exits.
        tcp_send_segment(&gateway_mac[0], target_ip, target_port, my_seq, peer_seq, TCP_FLAG_ACK, NULL, 0);
        if (peer_finished) {
            break;
        }
    }
    *response_len_out = total_received;

    // --- Best-effort graceful close - not required for the milestone's
    // own success criterion (a real handshake + real data exchange),
    // so a timeout here doesn't flip the overall return value. ---
    if (!peer_finished) {
        tcp_send_segment(&gateway_mac[0], target_ip, target_port, my_seq, peer_seq, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        if (tcp_wait_segment(target_ip, target_port, 500, &recv_buf[0], 1500, &seg)) {
            if ((seg.flags & TCP_FLAG_FIN) != 0) {
                peer_seq = seg.seq + 1;
                tcp_send_segment(&gateway_mac[0], target_ip, target_port, my_seq + 1, peer_seq, TCP_FLAG_ACK, NULL, 0);
            }
        }
    } else {
        tcp_send_segment(&gateway_mac[0], target_ip, target_port, my_seq, peer_seq, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        tcp_wait_segment(target_ip, target_port, 500, &recv_buf[0], 1500, &seg);   // best-effort final ACK, result unused
    }

    return total_received > 0;
}
