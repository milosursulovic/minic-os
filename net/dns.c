// Milestone 34: a minimal, hand-crafted DNS query - deliberately NOT a
// real DNS client (no compression, no caching, no multiple questions,
// no record types beyond A). This exists purely as a real, external
// verification vehicle for UDP, the same "one hardcoded real-protocol
// message as a test vehicle, not a general implementation" discipline
// the ARP request and ICMP ping both already used. QEMU SLIRP's own
// built-in DNS proxy (10.0.2.3, the same address the `arp` command
// already resolves) forwards a real query to a real resolver and sends
// back a real answer - genuinely external verification, not
// self-validation.

#include "dns.h"
#include "udp.h"
#include "ip.h"
#include "../isr/isr.h"

static const u16 DNS_PORT = 53;
static const u16 DNS_SRC_PORT = 12345;

// Encodes a dotted hostname ("example.com") into DNS label format:
// each segment prefixed by its own length byte, terminated by a zero
// length byte ("\7example\3com\0"). Bounded at 256 iterations - real
// headroom past any hostname this milestone actually uses.
static u16 dns_encode_name(u8* out, char* hostname) {
    u16 pos = 1;
    u16 len_pos = 0;
    u8 label_len = 0;
    int i = 0;
    while (i < 256) {
        char c = hostname[i];
        if (c == '\0') {
            out[len_pos] = label_len;
            out[pos] = 0;
            pos = pos + 1;
            return pos;
        }
        if (c == '.') {
            out[len_pos] = label_len;
            label_len = 0;
            len_pos = pos;
            pos = pos + 1;
        } else {
            out[pos] = (u8) c;
            pos = pos + 1;
            label_len = label_len + 1;
        }
        i = i + 1;
    }
    return pos;
}

// Builds a real, minimal DNS query message: a 12-byte header (one
// question, recursion desired, no answers/authority/additional records
// - a real, standard query shape) followed by the encoded QNAME, QTYPE
// (1 = A record), and QCLASS (1 = IN).
static u16 dns_build_query(u8* out, u16 transaction_id, char* hostname) {
    out[0] = (u8) (transaction_id >> 8);
    out[1] = (u8) (transaction_id & 0xFF);
    out[2] = 0x01;   // flags: standard query, recursion desired
    out[3] = 0x00;
    out[4] = 0x00;   // QDCOUNT = 1
    out[5] = 0x01;
    out[6] = 0x00;   // ANCOUNT = 0
    out[7] = 0x00;
    out[8] = 0x00;   // NSCOUNT = 0
    out[9] = 0x00;
    out[10] = 0x00;  // ARCOUNT = 0
    out[11] = 0x00;
    u16 pos = 12 + dns_encode_name(&out[12], hostname);
    out[pos] = 0x00;       // QTYPE = 1 (A)
    out[pos + 1] = 0x01;
    out[pos + 2] = 0x00;   // QCLASS = 1 (IN)
    out[pos + 3] = 0x01;
    return pos + 4;
}

// Sends the query to QEMU SLIRP's built-in DNS proxy over real UDP and
// verifies a genuinely matching response: the exact transaction ID this
// query sent (the same "identifier must match" discipline ARP/ICMP
// already used), the QR (response) bit set, and at least one real
// answer record - proving SLIRP's proxy actually resolved the name
// against a real resolver, not just echoed something malformed back.
bool dns_query(char* hostname) {
    u8 dns_proxy_ip[4];
    dns_proxy_ip[0] = 10;
    dns_proxy_ip[1] = 0;
    dns_proxy_ip[2] = 2;
    dns_proxy_ip[3] = 3;

    u8 query[96];
    u16 query_len = dns_build_query(&query[0], 0xABCD, hostname);

    if (!udp_send(&dns_proxy_ip[0], DNS_PORT, DNS_SRC_PORT, &query[0], query_len)) {
        return false;
    }

    u8 reply[160];
    u16 reply_len = udp_receive(&dns_proxy_ip[0], DNS_PORT, DNS_SRC_PORT, &reply[0], 160);
    if (reply_len < 12) {
        return false;
    }

    u16 reply_id = (((u16) reply[0]) << 8) | ((u16) reply[1]);
    bool is_response = (reply[2] & 0x80) != 0;
    u16 answer_count = (((u16) reply[6]) << 8) | ((u16) reply[7]);

    return reply_id == 0xABCD && is_response && answer_count > 0;
}
