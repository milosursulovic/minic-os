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

// Returns how many bytes the NAME field starting at buf[offset] occupies -
// either a 2-byte compression pointer (top two bits of the first byte set,
// the standard "point back at the question's own QNAME" DNS uses in every
// answer record in practice) or a real length-prefixed label sequence
// (walked exactly like dns_encode_name built one, terminated by a zero
// length byte). Bounded at 256 iterations for the literal-name path, same
// reasoning as dns_encode_name's own bound.
static u16 dns_skip_name(u8* buf, u16 offset) {
    if ((buf[offset] & 0xC0) == 0xC0) {
        return 2;
    }
    u16 pos = offset;
    int i = 0;
    while (i < 256) {
        u8 label_len = buf[pos];
        if (label_len == 0) {
            return (u16) (pos - offset + 1);
        }
        pos = (u16) (pos + 1 + label_len);
        i = i + 1;
    }
    return (u16) (pos - offset);
}

// A real minimal DNS resolver: sends the same kind of query dns_query()
// does, but actually parses the answer section instead of just checking
// the header - walks past the (echoed) question section, then each
// answer record's NAME/TYPE/CLASS/TTL/RDLENGTH/RDATA in turn, returning
// the first TYPE=1 (A) record's 4-byte address. Real DNS responses
// sometimes lead with a CNAME before the A record even for a name that
// has no real alias chain configured (a resolver's own caching/rewriting
// behavior) - skipping non-A records rather than assuming the first
// answer is always the one wanted is what makes this correct against a
// real upstream resolver, not just against SLIRP's own simplest case.
bool dns_resolve_a(char* hostname, u8* ip_out) {
    u8 dns_proxy_ip[4];
    dns_proxy_ip[0] = 10;
    dns_proxy_ip[1] = 0;
    dns_proxy_ip[2] = 2;
    dns_proxy_ip[3] = 3;

    u8 query[96];
    u16 query_len = dns_build_query(&query[0], 0xBEEF, hostname);

    if (!udp_send(&dns_proxy_ip[0], DNS_PORT, DNS_SRC_PORT, &query[0], query_len)) {
        return false;
    }

    u8 reply[256];
    u16 reply_len = udp_receive(&dns_proxy_ip[0], DNS_PORT, DNS_SRC_PORT, &reply[0], 256);
    if (reply_len < 12) {
        return false;
    }

    u16 reply_id = (((u16) reply[0]) << 8) | ((u16) reply[1]);
    bool is_response = (reply[2] & 0x80) != 0;
    u16 answer_count = (((u16) reply[6]) << 8) | ((u16) reply[7]);
    if (reply_id != 0xBEEF || !is_response || answer_count == 0) {
        return false;
    }

    // The question section starts right after the 12-byte header and is
    // exactly query_len - 12 bytes (RFC 1035 requires the server echo it
    // back unchanged for the reply to even be valid), so this doesn't
    // need to re-walk it - just skip straight to the answer section.
    u16 pos = query_len;
    u16 record = 0;
    while (record < answer_count && pos < reply_len) {
        pos = (u16) (pos + dns_skip_name(&reply[0], pos));
        if (pos + 10 > reply_len) {
            return false;
        }
        u16 rtype = (((u16) reply[pos]) << 8) | ((u16) reply[pos + 1]);
        u16 rdlength = (((u16) reply[pos + 8]) << 8) | ((u16) reply[pos + 9]);
        pos = (u16) (pos + 10);
        if (rtype == 1 && rdlength == 4 && pos + 4 <= reply_len) {
            ip_out[0] = reply[pos];
            ip_out[1] = reply[pos + 1];
            ip_out[2] = reply[pos + 2];
            ip_out[3] = reply[pos + 3];
            return true;
        }
        pos = (u16) (pos + rdlength);
        record = record + 1;
    }
    return false;
}
