// Hand-written DHCP client (RFC 2131) - DISCOVER/OFFER/REQUEST/ACK only,
// one lease per boot, no renewal (T1/T2 lease timers) - real, honestly
// scoped, same "client only, no gold-plating" boundary networking-
// completion arc item 1 (kernel/net/tcp/tcp.c's own retransmission)
// already established.
//
// DHCP's own bootstrap needs capabilities the general-purpose
// kernel/net/udp/udp.c API doesn't have: a broadcast destination with NO
// ARP (we don't have an IP yet to put in an ARP request's sender field),
// a source IP of 0.0.0.0, and "accept a reply from ANY source" (the
// server's own IP isn't known until the OFFER itself arrives). Rather
// than bolt broadcast/wildcard modes onto udp.c (used elsewhere with
// different assumptions), this follows kernel/net/tcp/tcp.c's own
// established precedent - a protocol-specific, self-contained frame
// builder/poller straight on top of e1000_send/e1000_receive, not a
// reuse of the generic UDP layer.

#include "dhcp.h"
#include "../ip/ip.h"
#include "../arp/arp.h"
#include "../e1000/e1000.h"
#include "../../isr/isr.h"
#include "../../sched/task.h"
#include "../../drivers/io/io.h"
#include "../../lib/strings.h"

static const u8 IP_PROTOCOL_UDP = 17;

static const u16 DHCP_CLIENT_PORT = 68;
static const u16 DHCP_SERVER_PORT = 67;
static const u32 DHCP_MAGIC_COOKIE = 0x63825363;

static const u8 DHCP_OP_BOOTREQUEST = 1;
static const u8 DHCP_OP_BOOTREPLY = 2;
static const u8 DHCP_HTYPE_ETHERNET = 1;
static const u8 DHCP_HLEN_ETHERNET = 6;

static const u8 DHCP_MSG_DISCOVER = 1;
static const u8 DHCP_MSG_OFFER = 2;
static const u8 DHCP_MSG_REQUEST = 3;
static const u8 DHCP_MSG_ACK = 5;

static const u8 DHCP_OPT_PAD = 0;
static const u8 DHCP_OPT_SUBNET_MASK = 1;
static const u8 DHCP_OPT_ROUTER = 3;
static const u8 DHCP_OPT_DNS = 6;
static const u8 DHCP_OPT_REQUESTED_IP = 50;
static const u8 DHCP_OPT_MSG_TYPE = 53;
static const u8 DHCP_OPT_SERVER_ID = 54;
static const u8 DHCP_OPT_PARAM_REQUEST_LIST = 55;
static const u8 DHCP_OPT_END = 255;

#define DHCP_RTO_TICKS 200
#define DHCP_MAX_ATTEMPTS 5

typedef struct {
    u8 yiaddr[4];
    u8 subnet_mask[4];
    u8 router[4];
    u8 dns_server[4];
    u8 server_id[4];
    bool has_subnet_mask;
    bool has_router;
    bool has_dns;
    bool has_server_id;
} dhcp_reply_info;

// Builds the fixed 236-byte BOOTP header + 4-byte magic cookie at out[0..239].
static void dhcp_build_fixed_header(u8* out, u32 xid, u8* client_mac) {
    int i = 0;
    while (i < 236) {
        out[i] = 0;
        i = i + 1;
    }
    out[0] = DHCP_OP_BOOTREQUEST;
    out[1] = DHCP_HTYPE_ETHERNET;
    out[2] = DHCP_HLEN_ETHERNET;
    out[3] = 0;  // hops
    out[4] = (u8) (xid >> 24);
    out[5] = (u8) (xid >> 16);
    out[6] = (u8) (xid >> 8);
    out[7] = (u8) xid;
    out[8] = 0;   // secs
    out[9] = 0;
    // flags: broadcast bit set - we have no real IP configured yet, so
    // any server reply must come back as a broadcast, not unicast.
    out[10] = 0x80;
    out[11] = 0x00;
    // ciaddr/yiaddr/siaddr/giaddr (offsets 12-27) already zeroed above.
    i = 0;
    while (i < 6) {
        out[28 + i] = client_mac[i];
        i = i + 1;
    }
    // sname/file (offsets 44-235) already zeroed above.
    out[236] = (u8) (DHCP_MAGIC_COOKIE >> 24);
    out[237] = (u8) (DHCP_MAGIC_COOKIE >> 16);
    out[238] = (u8) (DHCP_MAGIC_COOKIE >> 8);
    out[239] = (u8) DHCP_MAGIC_COOKIE;
}

// Builds a full Ethernet+IP+UDP+DHCP broadcast frame and sends it - no ARP
// anywhere in this path (DHCP bootstrap needs none: broadcast Ethernet
// destination, limited-broadcast IP destination, source IP forced to
// 0.0.0.0 since we don't have a real one yet).
static bool dhcp_send(u32 xid, u8* client_mac, u8 msg_type, u8* requested_ip, u8* server_id) {
    u8 frame[300];
    int i = 0;
    while (i < 6) {
        frame[i] = 0xFF;               // Ethernet dst: broadcast
        frame[6 + i] = client_mac[i];  // Ethernet src: our real MAC
        i = i + 1;
    }
    frame[12] = 0x08;
    frame[13] = 0x00;

    u8 zero_ip[4];
    zero_ip[0] = 0; zero_ip[1] = 0; zero_ip[2] = 0; zero_ip[3] = 0;
    u8 broadcast_ip[4];
    broadcast_ip[0] = 255; broadcast_ip[1] = 255; broadcast_ip[2] = 255; broadcast_ip[3] = 255;

    u8 dhcp_payload[260];
    dhcp_build_fixed_header(&dhcp_payload[0], xid, client_mac);
    u32 opt = 240;
    dhcp_payload[opt] = DHCP_OPT_MSG_TYPE;
    dhcp_payload[opt + 1] = 1;
    dhcp_payload[opt + 2] = msg_type;
    opt = opt + 3;
    if (msg_type == DHCP_MSG_REQUEST) {
        dhcp_payload[opt] = DHCP_OPT_REQUESTED_IP;
        dhcp_payload[opt + 1] = 4;
        dhcp_payload[opt + 2] = requested_ip[0];
        dhcp_payload[opt + 3] = requested_ip[1];
        dhcp_payload[opt + 4] = requested_ip[2];
        dhcp_payload[opt + 5] = requested_ip[3];
        opt = opt + 6;
        dhcp_payload[opt] = DHCP_OPT_SERVER_ID;
        dhcp_payload[opt + 1] = 4;
        dhcp_payload[opt + 2] = server_id[0];
        dhcp_payload[opt + 3] = server_id[1];
        dhcp_payload[opt + 4] = server_id[2];
        dhcp_payload[opt + 5] = server_id[3];
        opt = opt + 6;
    }
    dhcp_payload[opt] = DHCP_OPT_PARAM_REQUEST_LIST;
    dhcp_payload[opt + 1] = 3;
    dhcp_payload[opt + 2] = DHCP_OPT_SUBNET_MASK;
    dhcp_payload[opt + 3] = DHCP_OPT_ROUTER;
    dhcp_payload[opt + 4] = DHCP_OPT_DNS;
    opt = opt + 5;
    dhcp_payload[opt] = DHCP_OPT_END;
    opt = opt + 1;
    u16 dhcp_len = (u16) opt;

    ip_build_header(&frame[14], &zero_ip[0], &broadcast_ip[0], IP_PROTOCOL_UDP, (u16) (8 + dhcp_len));

    u16 udp_len = (u16) (8 + dhcp_len);
    frame[34] = (u8) (DHCP_CLIENT_PORT >> 8);
    frame[35] = (u8) (DHCP_CLIENT_PORT & 0xFF);
    frame[36] = (u8) (DHCP_SERVER_PORT >> 8);
    frame[37] = (u8) (DHCP_SERVER_PORT & 0xFF);
    frame[38] = (u8) (udp_len >> 8);
    frame[39] = (u8) (udp_len & 0xFF);
    // UDP checksum left at 0 - valid per RFC 768 for IPv4 (0 = "not
    // computed"), simplest honest choice given the source IP is 0.0.0.0
    // at this bootstrap stage.
    frame[40] = 0;
    frame[41] = 0;

    i = 0;
    while (i < dhcp_len) {
        frame[42 + i] = dhcp_payload[i];
        i = i + 1;
    }

    u16 frame_len = (u16) (42 + dhcp_len);
    return e1000_send(&frame[0], frame_len);
}

// Tick-bounded poll for a BOOTREPLY matching our own xid and the expected
// message type - deliberately NOT filtering by source IP (the one real
// capability neither kernel/net/udp/udp.c's udp_receive() nor
// kernel/net/tcp/tcp.c's tcp_wait_segment() have: the DHCP server's own IP
// isn't known until this exact reply tells us).
static bool dhcp_wait_reply(u32 xid, u8 expected_msg_type, u64 timeout_ticks, dhcp_reply_info* info_out) {
    u8 buf[600];
    u64 start_tick = g_tick_count;
    while (g_tick_count - start_tick < timeout_ticks) {
        yield();
        u16 len = e1000_receive(&buf[0], 600);
        if (len == 0) {
            continue;
        }
        bool is_ip = buf[12] == 0x08 && buf[13] == 0x00;
        if (!is_ip) {
            continue;
        }
        u8 proto = buf[14 + 9];
        if (proto != IP_PROTOCOL_UDP) {
            continue;
        }
        u16 dst_port = (((u16) buf[34 + 2]) << 8) | ((u16) buf[34 + 3]);
        if (dst_port != DHCP_CLIENT_PORT) {
            continue;
        }
        u32 dhcp_offset = 34 + 8;
        if ((u32) len < dhcp_offset + 240) {
            continue;  // too short to even hold the fixed header + magic cookie
        }
        u8* dhcp = &buf[dhcp_offset];
        if (dhcp[0] != DHCP_OP_BOOTREPLY) {
            continue;
        }
        u32 magic = (((u32) dhcp[236]) << 24) | (((u32) dhcp[237]) << 16)
                  | (((u32) dhcp[238]) << 8) | ((u32) dhcp[239]);
        if (magic != DHCP_MAGIC_COOKIE) {
            continue;
        }
        u32 reply_xid = (((u32) dhcp[4]) << 24) | (((u32) dhcp[5]) << 16)
                       | (((u32) dhcp[6]) << 8) | ((u32) dhcp[7]);
        if (reply_xid != xid) {
            continue;
        }

        u8 msg_type = 0;
        info_out->has_subnet_mask = false;
        info_out->has_router = false;
        info_out->has_dns = false;
        info_out->has_server_id = false;
        u32 max_o = (u32) len - dhcp_offset;
        u32 o = 240;
        while (o < max_o) {
            u8 opt_type = dhcp[o];
            if (opt_type == DHCP_OPT_END) {
                break;
            }
            if (opt_type == DHCP_OPT_PAD) {
                o = o + 1;
                continue;
            }
            u8 opt_len = dhcp[o + 1];
            if (opt_type == DHCP_OPT_MSG_TYPE) {
                msg_type = dhcp[o + 2];
            } else if (opt_type == DHCP_OPT_SUBNET_MASK && opt_len == 4) {
                info_out->subnet_mask[0] = dhcp[o + 2];
                info_out->subnet_mask[1] = dhcp[o + 3];
                info_out->subnet_mask[2] = dhcp[o + 4];
                info_out->subnet_mask[3] = dhcp[o + 5];
                info_out->has_subnet_mask = true;
            } else if (opt_type == DHCP_OPT_ROUTER && opt_len >= 4) {
                info_out->router[0] = dhcp[o + 2];
                info_out->router[1] = dhcp[o + 3];
                info_out->router[2] = dhcp[o + 4];
                info_out->router[3] = dhcp[o + 5];
                info_out->has_router = true;
            } else if (opt_type == DHCP_OPT_DNS && opt_len >= 4) {
                info_out->dns_server[0] = dhcp[o + 2];
                info_out->dns_server[1] = dhcp[o + 3];
                info_out->dns_server[2] = dhcp[o + 4];
                info_out->dns_server[3] = dhcp[o + 5];
                info_out->has_dns = true;
            } else if (opt_type == DHCP_OPT_SERVER_ID && opt_len == 4) {
                info_out->server_id[0] = dhcp[o + 2];
                info_out->server_id[1] = dhcp[o + 3];
                info_out->server_id[2] = dhcp[o + 4];
                info_out->server_id[3] = dhcp[o + 5];
                info_out->has_server_id = true;
            }
            o = o + 2 + opt_len;
        }
        if (msg_type != expected_msg_type) {
            continue;
        }

        info_out->yiaddr[0] = dhcp[16];
        info_out->yiaddr[1] = dhcp[17];
        info_out->yiaddr[2] = dhcp[18];
        info_out->yiaddr[3] = dhcp[19];
        return true;
    }
    return false;
}

static u32 ip_to_u32(u8* ip) {
    return (((u32) ip[0]) << 24) | (((u32) ip[1]) << 16) | (((u32) ip[2]) << 8) | ((u32) ip[3]);
}

bool dhcp_client(void) {
    if (!arp_init()) {
        serial_print("DHCP: no NIC present, using static fallback\n");
        return false;
    }
    u8 mac[6];
    e1000_get_mac(&mac[0]);
    // Tick-derived, not cryptographically random - fine, a DHCP xid only
    // needs to distinguish this exchange from a stale/unrelated one, same
    // "tick-derived" precedent kernel/net/tcp/tcp.c's own my_seq already uses.
    u32 xid = 0x44484300 + (u32) g_tick_count;

    dhcp_reply_info offer;
    bool got_offer = false;
    int attempt = 0;
    while (attempt < DHCP_MAX_ATTEMPTS) {
        if (!dhcp_send(xid, &mac[0], DHCP_MSG_DISCOVER, NULL, NULL)) {
            serial_print("DHCP: send failed, using static fallback\n");
            return false;
        }
        if (dhcp_wait_reply(xid, DHCP_MSG_OFFER, DHCP_RTO_TICKS, &offer)) {
            got_offer = true;
            break;
        }
        attempt = attempt + 1;
    }
    if (!got_offer) {
        serial_print("DHCP: no OFFER received, using static fallback\n");
        return false;
    }

    dhcp_reply_info ack;
    bool got_ack = false;
    attempt = 0;
    while (attempt < DHCP_MAX_ATTEMPTS) {
        if (!dhcp_send(xid, &mac[0], DHCP_MSG_REQUEST, &offer.yiaddr[0], &offer.server_id[0])) {
            serial_print("DHCP: send failed, using static fallback\n");
            return false;
        }
        if (dhcp_wait_reply(xid, DHCP_MSG_ACK, DHCP_RTO_TICKS, &ack)) {
            got_ack = true;
            break;
        }
        attempt = attempt + 1;
    }
    if (!got_ack) {
        serial_print("DHCP: no ACK received, using static fallback\n");
        return false;
    }

    g_my_ip[0] = ack.yiaddr[0];
    g_my_ip[1] = ack.yiaddr[1];
    g_my_ip[2] = ack.yiaddr[2];
    g_my_ip[3] = ack.yiaddr[3];
    if (ack.has_router) {
        g_gateway_ip[0] = ack.router[0];
        g_gateway_ip[1] = ack.router[1];
        g_gateway_ip[2] = ack.router[2];
        g_gateway_ip[3] = ack.router[3];
    }
    if (ack.has_dns) {
        g_dns_server_ip[0] = ack.dns_server[0];
        g_dns_server_ip[1] = ack.dns_server[1];
        g_dns_server_ip[2] = ack.dns_server[2];
        g_dns_server_ip[3] = ack.dns_server[3];
    }
    if (ack.has_subnet_mask) {
        g_subnet_mask[0] = ack.subnet_mask[0];
        g_subnet_mask[1] = ack.subnet_mask[1];
        g_subnet_mask[2] = ack.subnet_mask[2];
        g_subnet_mask[3] = ack.subnet_mask[3];
    }

    serial_print("DHCP: got real lease, ip=0x");
    print_hex((u64) ip_to_u32(&g_my_ip[0]));
    serial_print(" gateway=0x");
    print_hex((u64) ip_to_u32(&g_gateway_ip[0]));
    serial_print(" mask=0x");
    print_hex((u64) ip_to_u32(&g_subnet_mask[0]));
    serial_print(" dns=0x");
    print_hex((u64) ip_to_u32(&g_dns_server_ip[0]));
    serial_print("\n");
    return true;
}
