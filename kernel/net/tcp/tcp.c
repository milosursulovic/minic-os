// Real client-side retransmission (Faza I point 10, networking-completion
// arc item 1) via tcp_send_reliable() below - the listening/server side
// (tcp_listen/tcp_accept/tcp_server_send/tcp_server_receive) is explicitly
// NOT covered by this item, a stated scope boundary, not an oversight.
// Still no congestion control. A real connection table (below) gives each
// concurrent fetch its own local port, so two connections no longer collide.
//
// Off-subnet hosts are reached by sending the frame to the gateway's MAC
// (ARP-resolved) while the IP header's destination is the real remote host -
// routing, not ARP, decides the link-layer next hop.

#include "tcp.h"
#include "../ip/ip.h"
#include "../arp/arp.h"
#include "../e1000/e1000.h"
#include "../dns/dns.h"
#include "../../isr/isr.h"
#include "../../sched/task.h"
#include "../../drivers/io/io.h"
#include "../../lib/strings.h"

static const u8 IP_PROTOCOL_TCP = 6;

static const u8 TCP_FLAG_FIN = 0x01;
static const u8 TCP_FLAG_SYN = 0x02;
static const u8 TCP_FLAG_RST = 0x04;
static const u8 TCP_FLAG_PSH = 0x08;
static const u8 TCP_FLAG_ACK = 0x10;

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

// TEMPORARY test hook (Faza I point 10, networking-completion arc item 1:
// retransmission) - when nonzero, the next otherwise-valid match in
// tcp_wait_segment() below is silently discarded (as if it never arrived)
// and this counter is decremented, simulating one real lost packet
// without needing actual network-level packet loss. Lets tcp_send_reliable()'s
// retry path be verified for real (a genuine second attempt firing),
// not just that the zero-loss happy path still works. Remove before this
// item is considered fully done, unless kept as a permanent debug knob.
u32 g_tcp_debug_drop_count;

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
        if (g_tcp_debug_drop_count > 0) {
            g_tcp_debug_drop_count = g_tcp_debug_drop_count - 1;
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

#define TCP_RTO_TICKS 200
#define TCP_MAX_ATTEMPTS 5

// Real retransmission (Faza I point 10, networking-completion arc item 1):
// resends the IDENTICAL segment via tcp_send_segment() every TCP_RTO_TICKS
// while waiting for the peer's reply, instead of a single fire-and-forget
// send + one bounded wait. Tick-bounded, not count-bounded - same timing
// discipline proc/demo/ring3prog.c's own shmsync-check retry already
// established for this codebase's cooperative scheduler (a count-bounded
// loop can burn through many iterations inside one timer tick with zero
// real elapsed time). Returns exactly what tcp_wait_segment() would have -
// true the moment ANY matching segment arrives (the caller still does its
// own flag/ack-number validation on it, exactly as before this item);
// false only once every attempt is exhausted. This targets real packet
// loss (no reply at all within one RTO) - an unexpected/wrong reply
// arriving is a separate, much rarer case and stays the caller's own
// problem to validate, unchanged from before this item.
static bool tcp_send_reliable(u8* gateway_mac, u8* target_ip, u16 target_port, u16 local_port,
                               u32 seq, u32 ack, u8 flags, u8* payload, u16 payload_len,
                               u8* recv_buf, tcp_segment_info* info_out) {
    int attempt = 0;
    while (attempt < TCP_MAX_ATTEMPTS) {
        if (!tcp_send_segment(gateway_mac, target_ip, target_port, local_port, seq, ack, flags, payload, payload_len)) {
            return false;  // real send failure (e1000 TX ring stuck) - not worth retrying
        }
        if (tcp_wait_segment(target_ip, target_port, local_port, TCP_RTO_TICKS, recv_buf, 1500, info_out)) {
            if (attempt > 0) {
                serial_print("tcp_send_reliable: succeeded on attempt 0x");
                print_hex((u64) (attempt + 1));
                serial_print("\n");
            }
            return true;
        }
        attempt = attempt + 1;
    }
    return false;
}

static bool tcp_fetch_conn(u16 local_port, u8* target_ip, u16 target_port, const char* request, u16 request_len,
                            u8* response_out, u32 max_response_len, u32* response_len_out) {
    ensure_ip_configured();  // real DHCP now - must run before reading g_gateway_ip below, not after
    u8 gateway_mac[6];
    if (!arp_resolve(&g_gateway_ip[0], &gateway_mac[0])) {
        return false;
    }

    // Tick-derived, not cryptographically random - fine, nothing here needs that.
    u32 my_seq = 0x10000 + (u32) g_tick_count;
    u32 peer_seq = 0;

    u8 recv_buf[1500];
    tcp_segment_info seg;

    // --- Three-way handshake ---
    if (!tcp_send_reliable(&gateway_mac[0], target_ip, target_port, local_port, my_seq, 0, TCP_FLAG_SYN, NULL, 0,
                            &recv_buf[0], &seg)) {
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
    bool peer_finished = false;
    u32 total_received = 0;
    if (!tcp_send_reliable(&gateway_mac[0], target_ip, target_port, local_port, my_seq, peer_seq,
                            TCP_FLAG_PSH | TCP_FLAG_ACK, (u8*) request, request_len,
                            &recv_buf[0], &seg)) {
        return false;
    }
    my_seq = my_seq + request_len;
    // tcp_send_reliable() just consumed the peer's first reply into `seg`
    // (needed internally to know the data segment was actually acked,
    // not lost) - a real server commonly fuses its response payload onto
    // that same reply rather than sending a bare ACK first, so apply it
    // here with the exact same accounting the receive loop below uses,
    // instead of silently dropping whatever tcp_send_reliable() already
    // consumed.
    if (seg.payload_len > 0) {
        u32 copy_len = seg.payload_len;
        if (copy_len > max_response_len) {
            copy_len = max_response_len;
        }
        u32 i = 0;
        while (i < copy_len) {
            response_out[i] = recv_buf[seg.payload_offset + i];
            i = i + 1;
        }
        total_received = copy_len;
        peer_seq = peer_seq + seg.payload_len;
    }
    bool acked_something_new = seg.payload_len > 0;
    if ((seg.flags & TCP_FLAG_FIN) != 0) {
        peer_seq = peer_seq + 1;
        peer_finished = true;
        acked_something_new = true;
    }
    if (acked_something_new) {
        // A bare ACK (no payload, no FIN) needs no reply here - the peer
        // was just acking OUR data segment, nothing of its own to ack back.
        tcp_send_segment(&gateway_mac[0], target_ip, target_port, local_port, my_seq, peer_seq, TCP_FLAG_ACK, NULL, 0);
    }

    // Receive across as many segments as arrive within the budget, ACKing each,
    // until the peer sends FIN or the buffer fills.
    u64 recv_deadline = g_tick_count + 3000;
    while (!peer_finished && g_tick_count < recv_deadline && total_received < max_response_len) {
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

    // Best-effort close; a timeout here doesn't flip the overall return
    // value. Uses tcp_send_reliable() too now - a lost FIN no longer just
    // silently relies on the peer's own retransmit (real TCP stacks do
    // retransmit their own unacked FIN, but there's no reason to depend on
    // that when this side can just resend it directly).
    if (!peer_finished) {
        if (tcp_send_reliable(&gateway_mac[0], target_ip, target_port, local_port, my_seq, peer_seq,
                               TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0, &recv_buf[0], &seg)) {
            if ((seg.flags & TCP_FLAG_FIN) != 0) {
                peer_seq = seg.seq + 1;
                tcp_send_segment(&gateway_mac[0], target_ip, target_port, local_port, my_seq + 1, peer_seq, TCP_FLAG_ACK, NULL, 0);
            }
        }
    } else {
        tcp_send_reliable(&gateway_mac[0], target_ip, target_port, local_port, my_seq, peer_seq,
                           TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0, &recv_buf[0], &seg);   // best-effort final ACK, result unused
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

// --- Real server side: listen/accept ---

tcp_listener g_tcp_listeners[TCP_LISTENER_SLOTS];

int tcp_listen(u16 port) {
    // The client path gets this for free via arp_resolve()'s own internal
    // call to arp_init() - a server has no IP to resolve before it can
    // listen, so this calls the same lazy NIC-bring-up directly. Real bug
    // found during this milestone's own QEMU verification: without this,
    // e1000_receive() was being polled before the NIC/rings existed at
    // all, so tcp_accept() always timed out with zero data ever seen.
    if (!arp_init()) {
        return -1;
    }
    int i = 0;
    while (i < TCP_LISTENER_SLOTS) {
        if (!g_tcp_listeners[i].used) {
            g_tcp_listeners[i].used = true;
            g_tcp_listeners[i].port = port;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

// Same tick-bounded polling shape as tcp_wait_segment(), but the remote
// is unknown until a real SYN to local_port actually arrives - no
// src_ip/src_port filter, and the incoming frame's own source MAC
// (Ethernet header bytes 6-11) is captured so the reply can be addressed
// directly, no ARP round-trip needed.
static bool tcp_wait_syn(u16 local_port, u64 timeout_ticks, u8* buf, u16 buf_len,
                          u8* remote_mac_out, u8* remote_ip_out, u16* remote_port_out,
                          tcp_segment_info* info_out) {
    u64 start_tick = g_tick_count;
    while (g_tick_count - start_tick < timeout_ticks) {
        yield();
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
        u16 dst_port = (((u16) buf[36]) << 8) | ((u16) buf[37]);
        if (dst_port != local_port) {
            continue;
        }
        u8 flags = buf[47];
        if ((flags & TCP_FLAG_SYN) == 0) {
            continue;  // not a connection attempt
        }

        int i = 0;
        while (i < 6) {
            remote_mac_out[i] = buf[6 + i];
            i = i + 1;
        }
        i = 0;
        while (i < 4) {
            remote_ip_out[i] = buf[14 + 12 + i];
            i = i + 1;
        }
        *remote_port_out = (((u16) buf[34]) << 8) | ((u16) buf[35]);

        u16 ip_total_len = (((u16) buf[16]) << 8) | ((u16) buf[17]);
        u32 seq = (((u32) buf[38]) << 24) | (((u32) buf[39]) << 16) | (((u32) buf[40]) << 8) | ((u32) buf[41]);
        u16 tcp_header_len = (u16) ((buf[46] >> 4) * 4);
        info_out->seq = seq;
        info_out->flags = flags;
        info_out->payload_len = (u16) (ip_total_len - 20 - tcp_header_len);
        info_out->payload_offset = (u16) (34 + tcp_header_len);
        return true;
    }
    return false;
}

static int tcp_conn_alloc_server(u16 local_port, u8* remote_ip, u16 remote_port, u8* remote_mac) {
    int i = 0;
    while (i < TCP_CONNECTION_SLOTS) {
        if (!g_tcp_connections[i].used) {
            g_tcp_connections[i].used = true;
            g_tcp_connections[i].local_port = local_port;
            int j = 0;
            while (j < 4) {
                g_tcp_connections[i].remote_ip[j] = remote_ip[j];
                j = j + 1;
            }
            g_tcp_connections[i].remote_port = remote_port;
            j = 0;
            while (j < 6) {
                g_tcp_connections[i].remote_mac[j] = remote_mac[j];
                j = j + 1;
            }
            return i;
        }
        i = i + 1;
    }
    return -1;
}

int tcp_accept(int listener_slot, u64 timeout_ticks, u8* remote_ip_out, u16* remote_port_out) {
    u16 local_port = g_tcp_listeners[listener_slot].port;

    u8 remote_mac[6];
    u8 remote_ip[4];
    u16 remote_port;
    u8 recv_buf[1500];
    tcp_segment_info syn_info;
    if (!tcp_wait_syn(local_port, timeout_ticks, &recv_buf[0], 1500, &remote_mac[0], &remote_ip[0],
                       &remote_port, &syn_info)) {
        return -1;
    }

    int slot = tcp_conn_alloc_server(local_port, &remote_ip[0], remote_port, &remote_mac[0]);
    if (slot < 0) {
        return -1;
    }

    u32 my_seq = 0x20000 + (u32) g_tick_count;
    u32 peer_seq = syn_info.seq + 1;
    if (!tcp_send_segment(&remote_mac[0], &remote_ip[0], remote_port, local_port, my_seq, peer_seq,
                           TCP_FLAG_SYN | TCP_FLAG_ACK, NULL, 0)) {
        tcp_conn_close(slot);
        return -1;
    }

    tcp_segment_info ack_info;
    if (!tcp_wait_segment(&remote_ip[0], remote_port, local_port, timeout_ticks, &recv_buf[0], 1500, &ack_info)) {
        tcp_conn_close(slot);
        return -1;
    }
    if ((ack_info.flags & TCP_FLAG_ACK) == 0 || ack_info.ack != my_seq + 1) {
        tcp_conn_close(slot);
        return -1;
    }
    my_seq = my_seq + 1;

    g_tcp_connections[slot].my_seq = my_seq;
    g_tcp_connections[slot].peer_seq = peer_seq;

    int i = 0;
    while (i < 4) {
        remote_ip_out[i] = remote_ip[i];
        i = i + 1;
    }
    *remote_port_out = remote_port;
    return slot;
}

u32 tcp_server_send(int conn_slot, const u8* data, u16 len) {
    tcp_connection* c = &g_tcp_connections[conn_slot];
    bool ok = tcp_send_segment(&c->remote_mac[0], &c->remote_ip[0], c->remote_port, c->local_port,
                                c->my_seq, c->peer_seq, TCP_FLAG_PSH | TCP_FLAG_ACK, (u8*) data, len);
    if (!ok) {
        return 0;
    }
    c->my_seq = c->my_seq + len;
    return len;
}

u32 tcp_server_receive(int conn_slot, u8* buf, u32 max_len, u64 timeout_ticks) {
    tcp_connection* c = &g_tcp_connections[conn_slot];
    u8 recv_buf[1500];
    tcp_segment_info seg;
    u64 deadline = g_tick_count + timeout_ticks;

    // A real bare ACK (payload_len==0, e.g. the client's own automatic
    // ACK of our last echo) genuinely arrives on the wire and matches
    // tcp_wait_segment()'s filter - it must be skipped, not mistaken for
    // "the next message" the way a single unconditional wait would (a
    // real bug found via this milestone's own QEMU verification: round 2
    // of a 3-round echo test returned n=0 almost instantly instead of
    // genuinely waiting, because the client's ACK of round 1's echo was
    // consumed as if it were round 2's data). tcp_fetch_conn's own
    // client-side receive loop already has an equivalent skip (its
    // `if (seg.payload_len > 0)` guard, inside a loop that keeps waiting
    // otherwise) - this mirrors that for the single-call server API.
    while (g_tick_count < deadline) {
        if (!tcp_wait_segment(&c->remote_ip[0], c->remote_port, c->local_port, deadline - g_tick_count,
                               &recv_buf[0], 1500, &seg)) {
            return 0;
        }
        if (seg.payload_len == 0 && (seg.flags & TCP_FLAG_FIN) == 0) {
            continue;  // bare ACK - not real data, keep waiting
        }

        u32 copy_len = seg.payload_len;
        if (copy_len > max_len) {
            copy_len = max_len;
        }
        u32 i = 0;
        while (i < copy_len) {
            buf[i] = recv_buf[seg.payload_offset + i];
            i = i + 1;
        }
        c->peer_seq = c->peer_seq + seg.payload_len;
        // ACK what was just received, keeping seq state honest for later calls.
        tcp_send_segment(&c->remote_mac[0], &c->remote_ip[0], c->remote_port, c->local_port,
                          c->my_seq, c->peer_seq, TCP_FLAG_ACK, NULL, 0);
        if ((seg.flags & TCP_FLAG_FIN) != 0) {
            c->peer_seq = c->peer_seq + 1;
        }
        return copy_len;
    }
    return 0;
}

void tcp_server_close(int conn_slot) {
    tcp_connection* c = &g_tcp_connections[conn_slot];
    u8 recv_buf[1500];
    tcp_segment_info seg;
    tcp_send_segment(&c->remote_mac[0], &c->remote_ip[0], c->remote_port, c->local_port,
                      c->my_seq, c->peer_seq, TCP_FLAG_FIN | TCP_FLAG_ACK, NULL, 0);
    // Best-effort final exchange, same tone as tcp_fetch_conn's own close.
    if (tcp_wait_segment(&c->remote_ip[0], c->remote_port, c->local_port, 500, &recv_buf[0], 1500, &seg)) {
        if ((seg.flags & TCP_FLAG_FIN) != 0) {
            c->peer_seq = seg.seq + 1;
            tcp_send_segment(&c->remote_mac[0], &c->remote_ip[0], c->remote_port, c->local_port,
                              c->my_seq + 1, c->peer_seq, TCP_FLAG_ACK, NULL, 0);
        }
    }
    tcp_conn_close(conn_slot);
}
