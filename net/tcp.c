// TCP client only: no listening side, no retransmission/congestion
// control. A real connection table (below) gives each concurrent fetch
// its own local port, so two connections no longer collide.
//
// Off-subnet hosts are reached by sending the frame to the gateway's MAC
// (ARP-resolved) while the IP header's destination is the real remote host -
// routing, not ARP, decides the link-layer next hop.

#include "tcp.h"
#include "ip.h"
#include "arp.h"
#include "e1000.h"
#include "dns.h"
#include "../isr/isr.h"
#include "../sched/task.h"

static const u8 IP_PROTOCOL_TCP = 6;

static const u8 TCP_FLAG_FIN = 0x01;
static const u8 TCP_FLAG_SYN = 0x02;
static const u8 TCP_FLAG_RST = 0x04;
static const u8 TCP_FLAG_PSH = 0x08;
static const u8 TCP_FLAG_ACK = 0x10;

static const u8 GATEWAY_IP[4] = {10, 0, 2, 2};

// 12-byte pseudo-header (src/dst IP, protocol 6, segment length) + TCP segment.
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

// Payload (already placed by the caller at out[20..]) must exist before this runs,
// since the checksum covers it.
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

static const u16 TCP_WINDOW = 8192;

static const u16 TCP_BASE_LOCAL_PORT = 43981;

tcp_connection g_tcp_connections[TCP_CONNECTION_SLOTS];

static int tcp_conn_open(u8* remote_ip, u16 remote_port) {
    int i = 0;
    while (i < TCP_CONNECTION_SLOTS) {
        if (!g_tcp_connections[i].used) {
            g_tcp_connections[i].used = true;
            g_tcp_connections[i].local_port = (u16) (TCP_BASE_LOCAL_PORT + i);
            int j = 0;
            while (j < 4) {
                g_tcp_connections[i].remote_ip[j] = remote_ip[j];
                j = j + 1;
            }
            g_tcp_connections[i].remote_port = remote_port;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

static void tcp_conn_close(int slot) {
    g_tcp_connections[slot].used = false;
}

// Every send goes through this helper, so the gateway-vs-destination
// distinction is handled in exactly one place.
static bool tcp_send_segment(u8* gateway_mac, u8* target_ip, u16 target_port, u16 local_port,
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
    tcp_build_header(&frame[34], local_port, target_port, seq, ack, flags, TCP_WINDOW,
                      &g_my_ip[0], target_ip, payload_len);

    u16 frame_len = (u16) (54 + payload_len);
    return e1000_send(&frame[0], frame_len);
}

typedef struct {
    u32 seq;
    u32 ack;
    u8 flags;
    u16 payload_len;
    u16 payload_offset;   // into the caller's own receive buffer
} tcp_segment_info;

// Tick-bounded poll for a segment genuinely from target_ip:target_port to local_port.
static bool tcp_wait_segment(u8* target_ip, u16 target_port, u16 local_port, u64 timeout_ticks,
                              u8* buf, u16 buf_len, tcp_segment_info* info_out) {
    u64 start_tick = g_tick_count;
    while (g_tick_count - start_tick < timeout_ticks) {
        yield();  // this runs on a background worker task now
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
        if (src_port != target_port || dst_port != local_port) {
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

static bool tcp_fetch_conn(u16 local_port, u8* target_ip, u16 target_port, const char* request, u16 request_len,
                            u8* response_out, u32 max_response_len, u32* response_len_out) {
    u8 gateway_mac[6];
    if (!arp_resolve((u8*) &GATEWAY_IP[0], &gateway_mac[0])) {
        return false;
    }
    ip_init();

    // Tick-derived, not cryptographically random - fine, nothing here needs that.
    u32 my_seq = 0x10000 + (u32) g_tick_count;
    u32 peer_seq = 0;

    u8 recv_buf[1500];
    tcp_segment_info seg;

    // --- Three-way handshake ---
    if (!tcp_send_segment(&gateway_mac[0], target_ip, target_port, local_port, my_seq, 0, TCP_FLAG_SYN, NULL, 0)) {
        return false;
    }
    if (!tcp_wait_segment(target_ip, target_port, local_port, 3000, &recv_buf[0], 1500, &seg)) {
        return false;
    }
    if ((seg.flags & TCP_FLAG_RST) != 0) {
        return false;   // connection refused
    }
    if ((seg.flags & TCP_FLAG_SYN) == 0 || (seg.flags & TCP_FLAG_ACK) == 0 || seg.ack != my_seq + 1) {
        return false;   // not a matching SYN-ACK
    }
    my_seq = my_seq + 1;
    peer_seq = seg.seq + 1;
    if (!tcp_send_segment(&gateway_mac[0], target_ip, target_port, local_port, my_seq, peer_seq, TCP_FLAG_ACK, NULL, 0)) {
        return false;
    }
    // Handshake complete - ESTABLISHED.

    // --- Send the real request as one data segment ---
    if (!tcp_send_segment(&gateway_mac[0], target_ip, target_port, local_port, my_seq, peer_seq,
                           TCP_FLAG_PSH | TCP_FLAG_ACK, (u8*) request, request_len)) {
        return false;
    }
    my_seq = my_seq + request_len;

    // Receive across as many segments as arrive within the budget, ACKing each,
    // until the peer sends FIN or the buffer fills.
    bool peer_finished = false;
    u32 total_received = 0;
    u64 recv_deadline = g_tick_count + 3000;
    while (g_tick_count < recv_deadline && total_received < max_response_len) {
        if (!tcp_wait_segment(target_ip, target_port, local_port, recv_deadline - g_tick_count,
                               &recv_buf[0], 1500, &seg)) {
            break;   // timed out - stop with whatever arrived
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
        // ACK whatever was just processed - keeps peer_seq/my_seq honest for the close below.
        tcp_send_segment(&gateway_mac[0], target_ip, target_port, local_port, my_seq, peer_seq, TCP_FLAG_ACK, NULL, 0);
        if (peer_finished) {
            break;
        }
    }
    *response_len_out = total_received;

    // Best-effort close; a timeout here doesn't flip the overall return value.
    if (!peer_finished) {
        tcp_send_segment(&gateway_mac[0], target_ip, target_port, local_port, my_seq, peer_seq, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        if (tcp_wait_segment(target_ip, target_port, local_port, 500, &recv_buf[0], 1500, &seg)) {
            if ((seg.flags & TCP_FLAG_FIN) != 0) {
                peer_seq = seg.seq + 1;
                tcp_send_segment(&gateway_mac[0], target_ip, target_port, local_port, my_seq + 1, peer_seq, TCP_FLAG_ACK, NULL, 0);
            }
        }
    } else {
        tcp_send_segment(&gateway_mac[0], target_ip, target_port, local_port, my_seq, peer_seq, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
        tcp_wait_segment(target_ip, target_port, local_port, 500, &recv_buf[0], 1500, &seg);   // best-effort final ACK, result unused
    }

    return total_received > 0;
}

bool tcp_fetch(u8* target_ip, u16 target_port, const char* request, u16 request_len,
               u8* response_out, u32 max_response_len, u32* response_len_out) {
    *response_len_out = 0;
    int slot = tcp_conn_open(target_ip, target_port);
    if (slot < 0) {
        return false;
    }
    bool ok = tcp_fetch_conn(g_tcp_connections[slot].local_port, target_ip, target_port,
                              request, request_len, response_out, max_response_len, response_len_out);
    tcp_conn_close(slot);
    return ok;
}
