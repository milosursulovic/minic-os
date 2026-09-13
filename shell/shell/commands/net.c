#include "net.h"
#include "../../../kernel/drivers/io/io.h"
#include "../../../kernel/drivers/keyboard/keyboard.h"
#include "../../../kernel/lib/strings.h"
#include "../../../kernel/isr/isr.h"
#include "../../../kernel/net/e1000/e1000.h"
#include "../../../kernel/net/arp/arp.h"
#include "../../../kernel/net/ip/ip.h"
#include "../../../kernel/net/icmp/icmp.h"
#include "../../../kernel/net/dns/dns.h"
#include "../../../kernel/net/tcp/tcp.h"
#include "../../../kernel/net/ipv6/ipv6.h"
#include "../../../kernel/net/ndp/ndp.h"
#include "../../../kernel/net/icmp6/icmp6.h"
#include "../../../kernel/net/tls/tls.h"

void print_mac(u8* mac) {
    int i = 0;
    while (i < 6) {
        print_hex((u64) mac[i]);
        if (i < 5) {
            vga_print(":");
            serial_print(":");
        }
        i = i + 1;
    }
}

// Dotted-decimal, not hex - a real IP address, unlike a MAC, is expected
// in decimal by every human and every other tool that will ever look at
// this output (ping/ipconfig/traceroute/etc all agree on this).
static void print_ip(u8* ip) {
    int i = 0;
    while (i < 4) {
        print_decimal((u64) ip[i]);
        if (i < 3) {
            vga_print(".");
            serial_print(".");
        }
        i = i + 1;
    }
}

// Colon-hex, one 16-bit group at a time, no zero-run "::" compression
// (RFC 5952's compression is a display nicety, not required for the
// address to be unambiguous or correct - kept simple, same "correctness
// over cleverness" spirit as every other hand-written primitive here).
static void print_ip6(u8* ip6) {
    int i = 0;
    while (i < 16) {
        u16 group = (((u16) ip6[i]) << 8) | ((u16) ip6[i + 1]);
        print_hex((u64) group);
        if (i < 14) {
            vga_print(":");
            serial_print(":");
        }
        i = i + 2;
    }
}

void cmd_netconns(void) {
    vga_print("tcp connections:");
    serial_print("tcp connections:");
    int i = 0;
    while (i < TCP_CONNECTION_SLOTS) {
        if (g_tcp_connections[i].used) {
            vga_print(" conn");
            serial_print(" conn");
            print_hex((u64) i);
            vga_print(" local_port=0x");
            serial_print(" local_port=0x");
            print_hex((u64) g_tcp_connections[i].local_port);
            vga_print(" remote=0x");
            serial_print(" remote=0x");
            print_hex((u64) g_tcp_connections[i].remote_ip[0]);
            vga_print(".0x");
            serial_print(".0x");
            print_hex((u64) g_tcp_connections[i].remote_ip[1]);
            vga_print(".0x");
            serial_print(".0x");
            print_hex((u64) g_tcp_connections[i].remote_ip[2]);
            vga_print(".0x");
            serial_print(".0x");
            print_hex((u64) g_tcp_connections[i].remote_ip[3]);
            vga_print(":0x");
            serial_print(":0x");
            print_hex((u64) g_tcp_connections[i].remote_port);
        }
        i = i + 1;
    }
}

// Resolves the gateway, resolves it again (cache hit), resolves the DNS
// proxy, and resolves an unreachable address (must fail cleanly).
void cmd_arp(void) {
    ensure_ip_configured();  // real DHCP now - must run before reading g_gateway_ip/g_dns_server_ip below

    u8 mac[6];
    u64 t0 = g_tick_count;
    bool ok1 = arp_resolve(&g_gateway_ip[0], &mac[0]);
    u64 elapsed1 = g_tick_count - t0;
    vga_print("resolve gateway ok=0x");
    serial_print("resolve gateway ok=0x");
    print_hex((u64) ok1);
    if (ok1) {
        vga_print(" mac=");
        serial_print(" mac=");
        print_mac(&mac[0]);
    }
    vga_print(" elapsed_ticks=0x");
    serial_print(" elapsed_ticks=0x");
    print_hex(elapsed1);

    u8 mac2[6];
    u64 t1 = g_tick_count;
    bool ok2 = arp_resolve(&g_gateway_ip[0], &mac2[0]);
    u64 elapsed2 = g_tick_count - t1;
    vga_print(" cached_ok=0x");
    serial_print(" cached_ok=0x");
    print_hex((u64) ok2);
    vga_print(" cached_elapsed_ticks=0x");
    serial_print(" cached_elapsed_ticks=0x");
    print_hex(elapsed2);

    u8 mac3[6];
    bool ok3 = arp_resolve(&g_dns_server_ip[0], &mac3[0]);
    vga_print(" resolve_dns_proxy_ok=0x");
    serial_print(" resolve_dns_proxy_ok=0x");
    print_hex((u64) ok3);
    if (ok3) {
        vga_print(" dns_mac=");
        serial_print(" dns_mac=");
        print_mac(&mac3[0]);
    }

    u8 unreachable_ip[4];
    unreachable_ip[0] = 10;
    unreachable_ip[1] = 0;
    unreachable_ip[2] = 2;
    unreachable_ip[3] = 99;
    u8 mac4[6];
    bool ok4 = arp_resolve(&unreachable_ip[0], &mac4[0]);
    vga_print(" resolve_unreachable_ok=0x");
    serial_print(" resolve_unreachable_ok=0x");
    print_hex((u64) ok4);
}

// ping <host-or-ip> - a literal dotted-decimal IP is used as-is
// (parse_ip); anything else is treated as a hostname and resolved via
// the existing (previously unused by any command) dns_resolve_a(). Then
// 4 real ICMP echo requests (icmp_ping() genuinely sends/waits for a
// matching reply, not simulated), same real-shaped output every
// Linux/Windows ping gives: one line per reply (or a timeout), then a
// summary. There's no sub-tick timer here, and QEMU/TCG's real tick rate
// is unreliable (see CLAUDE.md) - the "ms" figure is real ticks × 10
// (nominal 100Hz), labeled "~" rather than presented as false precision.
#define PING_COUNT 4
#define PING_IDENTIFIER 0x1234
void cmd_ping(void) {
    char* arg = &g_line_buffer[5];  // past "ping "
    if (arg[0] == '\0') {
        vga_print("usage: ping <host-or-ip>");
        serial_print("usage: ping <host-or-ip>");
        return;
    }

    u8 target_ip[4];
    if (!parse_ip(arg, target_ip)) {
        if (!dns_resolve_a(arg, target_ip)) {
            vga_print("ping: could not resolve ");
            serial_print("ping: could not resolve ");
            vga_print(arg);
            serial_print(arg);
            return;
        }
    }

    vga_print("PING ");
    serial_print("PING ");
    print_ip(target_ip);
    vga_print("  ");
    serial_print("  ");

    int received = 0;
    int seq = 1;
    while (seq <= PING_COUNT) {
        u64 start_tick = g_tick_count;
        bool ok = icmp_ping(target_ip, PING_IDENTIFIER, (u16) seq);
        u64 elapsed = g_tick_count - start_tick;

        if (ok) {
            received = received + 1;
            vga_print("12 bytes from ");
            serial_print("12 bytes from ");
            print_ip(target_ip);
            vga_print(": icmp_seq=");
            serial_print(": icmp_seq=");
            print_decimal((u64) seq);
            vga_print(" ttl=64 time=~");
            serial_print(" ttl=64 time=~");
            print_decimal(elapsed * 10);
            vga_print("ms  ");
            serial_print("ms  ");
        } else {
            vga_print("Request timeout for icmp_seq=");
            serial_print("Request timeout for icmp_seq=");
            print_decimal((u64) seq);
            vga_print("  ");
            serial_print("  ");
        }
        seq = seq + 1;
    }

    print_decimal((u64) PING_COUNT);
    vga_print(" transmitted, ");
    serial_print(" transmitted, ");
    print_decimal((u64) received);
    vga_print(" received");
    serial_print(" received");
}

void cmd_ipconfig(void) {
    ensure_ip_configured();  // real DHCP now (kernel/net/dhcp/) - must run before reading the globals below

    vga_print("IP: ");
    serial_print("IP: ");
    print_ip(g_my_ip);
    vga_print("  MASK: ");
    serial_print("  MASK: ");
    print_ip(g_subnet_mask);
    vga_print("  GATEWAY: ");
    serial_print("  GATEWAY: ");
    print_ip(g_gateway_ip);
    vga_print("  DNS: ");
    serial_print("  DNS: ");
    print_ip(g_dns_server_ip);

    bool ok = e1000_init();
    if (!ok) {
        vga_print("  MAC: (e1000 not found)");
        serial_print("  MAC: (e1000 not found)");
        return;
    }
    u8 mac[6];
    e1000_get_mac(&mac[0]);
    vga_print("  MAC: ");
    serial_print("  MAC: ");
    print_mac(&mac[0]);
    vga_print("  LINK: ");
    serial_print("  LINK: ");
    vga_print(e1000_link_up() ? "UP" : "DOWN");
    serial_print(e1000_link_up() ? "UP" : "DOWN");
}

// Faza I point 10, networking-completion arc item 3: real IPv6. Link-local
// is always deterministic (no network needed); the global address (SLAAC,
// via a real Router Advertisement) is best-effort - shown only if one was
// actually obtained, same "don't claim what isn't real" honesty
// ensure_ip_configured()'s DHCP fallback already established.
void cmd_ipv6config(void) {
    arp_init();  // brings the NIC up if needed - same call kernel/net/tcp/tcp.c's tcp_listen() already uses with no IP available yet
    ipv6_init_link_local();
    bool got_slaac = ndp_do_slaac();

    vga_print("LINK-LOCAL: ");
    serial_print("LINK-LOCAL: ");
    print_ip6(&g_my_ipv6_link_local[0]);

    if (got_slaac && g_ipv6_global_valid) {
        vga_print("  GLOBAL: ");
        serial_print("  GLOBAL: ");
        print_ip6(&g_my_ipv6_global[0]);
        vga_print("  ROUTER: ");
        serial_print("  ROUTER: ");
        print_ip6(&g_ipv6_default_router[0]);
    } else {
        vga_print("  GLOBAL: (no Router Advertisement received)");
        serial_print("  GLOBAL: (no Router Advertisement received)");
    }
}

// Real ICMPv6 echo (ping6), same real-shaped output cmd_ping()'s own IPv4
// version gives. With no argument, pings kernel/net/ipv6/ipv6.h's own
// g_ipv6_default_router (the SLAAC Router Advertisement's source) -
// avoids requiring the user to type a colon-hex literal at this kernel's
// shift-less console keyboard for the primary verification path, while
// still accepting a real typed address for anything else.
#define PING6_COUNT 4
#define PING6_IDENTIFIER 0x5678
void cmd_ping6(void) {
    // "ping6" alone (bare, no trailing space) and "ping6 <address>" both
    // reach here (shell.c's own dispatch matches both) - g_line_buffer[5]
    // is either '\0' (bare) or ' ' (an address follows at [6]).
    char* arg = (g_line_buffer[5] == ' ') ? &g_line_buffer[6] : &g_line_buffer[5];
    u8 target_ip6[16];

    if (arg[0] == '\0') {
        if (!g_ipv6_global_valid) {
            arp_init();
            ipv6_init_link_local();
            ndp_do_slaac();
        }
        if (!g_ipv6_global_valid) {
            vga_print("ping6: no address given and no default router known (run ipv6config first)");
            serial_print("ping6: no address given and no default router known (run ipv6config first)");
            return;
        }
        int i = 0;
        while (i < 16) {
            target_ip6[i] = g_ipv6_default_router[i];
            i = i + 1;
        }
    } else if (!parse_ip6(arg, target_ip6)) {
        vga_print("usage: ping6 [address]");
        serial_print("usage: ping6 [address]");
        return;
    }

    vga_print("PING6 ");
    serial_print("PING6 ");
    print_ip6(target_ip6);
    vga_print("  ");
    serial_print("  ");

    int received = 0;
    int seq = 1;
    while (seq <= PING6_COUNT) {
        u64 start_tick = g_tick_count;
        bool ok = icmp6_ping(target_ip6, PING6_IDENTIFIER, (u16) seq);
        u64 elapsed = g_tick_count - start_tick;

        if (ok) {
            received = received + 1;
            vga_print("bytes from ");
            serial_print("bytes from ");
            print_ip6(target_ip6);
            vga_print(": icmp6_seq=");
            serial_print(": icmp6_seq=");
            print_decimal((u64) seq);
            vga_print(" time=~");
            serial_print(" time=~");
            print_decimal(elapsed * 10);
            vga_print("ms  ");
            serial_print("ms  ");
        } else {
            vga_print("Request timeout for icmp6_seq=");
            serial_print("Request timeout for icmp6_seq=");
            print_decimal((u64) seq);
            vga_print("  ");
            serial_print("  ");
        }
        seq = seq + 1;
    }

    print_decimal((u64) PING6_COUNT);
    vga_print(" transmitted, ");
    serial_print(" transmitted, ");
    print_decimal((u64) received);
    vga_print(" received");
    serial_print(" received");
}

void cmd_dns(void) {
    u64 start_tick = g_tick_count;
    bool ok = dns_query("example.com");
    u64 elapsed = g_tick_count - start_tick;

    vga_print("dns query ok=0x");
    serial_print("dns query ok=0x");
    print_hex((u64) ok);
    vga_print(" elapsed_ticks=0x");
    serial_print(" elapsed_ticks=0x");
    print_hex(elapsed);
}

void cmd_tcp(void) {
    u8 ip[4];
    if (!dns_resolve_a("example.com", &ip[0])) {
        vga_print("tcp: could not resolve example.com");
        serial_print("tcp: could not resolve example.com");
        return;
    }
    vga_print("resolved example.com -> 0x");
    serial_print("resolved example.com -> 0x");
    print_hex(ip[0]);
    vga_print(".0x");
    serial_print(".0x");
    print_hex(ip[1]);
    vga_print(".0x");
    serial_print(".0x");
    print_hex(ip[2]);
    vga_print(".0x");
    serial_print(".0x");
    print_hex(ip[3]);

    const char* request = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    u8 response[512];
    u32 response_len = 0;
    u64 start_tick = g_tick_count;
    bool ok = tcp_fetch(&ip[0], 80, request, (u16) strlen_(request), &response[0], 512, &response_len);
    u64 elapsed = g_tick_count - start_tick;

    vga_print(" tcp_fetch_ok=0x");
    serial_print(" tcp_fetch_ok=0x");
    print_hex((u64) ok);
    vga_print(" response_len=0x");
    serial_print(" response_len=0x");
    print_hex((u64) response_len);
    vga_print(" elapsed_ticks=0x");
    serial_print(" elapsed_ticks=0x");
    print_hex(elapsed);

    bool got_http_status = response_len >= 4
        && response[0] == 'H' && response[1] == 'T' && response[2] == 'T' && response[3] == 'P';
    vga_print(" got_http_status=0x");
    serial_print(" got_http_status=0x");
    print_hex((u64) got_http_status);
}

// Real hand-written TLS 1.2 client (kernel/net/tls/tls.c) - RSA key
// exchange, AES-128-CBC, HMAC-SHA256, trust-on-first-use. No argument
// defaults to a local test server (10.0.2.2:8443 - QEMU SLIRP's own
// gateway address, which reaches services on the HOST's own loopback,
// e.g. `openssl s_server -accept 8443 -cipher AES128-SHA256 -tls1_2`) -
// TLS_RSA_WITH_AES_128_CBC_SHA256 has been dropped by most real internet
// hosts (no forward secrecy), so a real, independent local peer is this
// item's own decisive interop proof, same spirit as the TCP server
// milestone's own "verified with a real external client" precedent.
void cmd_tlsfetch(void) {
    char* arg = (g_line_buffer[8] == ' ') ? &g_line_buffer[9] : &g_line_buffer[8];

    u8 ip[4];
    ip[0] = 10; ip[1] = 0; ip[2] = 2; ip[3] = 2;
    u32 port = 8443;

    if (arg[0] != '\0') {
        char ip_buf[32];
        int i = 0;
        while (arg[i] != '\0' && arg[i] != ' ' && i < 31) {
            ip_buf[i] = arg[i];
            i = i + 1;
        }
        ip_buf[i] = '\0';
        if (!parse_ip(ip_buf, ip)) {
            vga_print("tlsfetch: usage: tlsfetch [ip] [port]");
            serial_print("tlsfetch: usage: tlsfetch [ip] [port]");
            return;
        }
        if (arg[i] == ' ') {
            port = parse_decimal_u32(&arg[i + 1]);
        }
    }

    vga_print("tlsfetch: connecting to ");
    serial_print("tlsfetch: connecting to ");
    print_ip(ip);
    vga_print(":");
    serial_print(":");
    print_decimal((u64) port);
    vga_print("\n");
    serial_print("\n");

    tls_conn_t conn;
    u64 start_tick = g_tick_count;
    bool handshake_ok = tls_connect(ip, (u16) port, &conn);
    u64 handshake_ticks = g_tick_count - start_tick;

    vga_print("tls_connect_ok=0x");
    serial_print("tls_connect_ok=0x");
    print_hex((u64) handshake_ok);
    vga_print(" elapsed_ticks=0x");
    serial_print(" elapsed_ticks=0x");
    print_hex(handshake_ticks);
    vga_print("\n");
    serial_print("\n");

    if (!handshake_ok) {
        return;
    }

    const char* request = "GET / HTTP/1.1\r\nHost: test\r\nConnection: close\r\n\r\n";
    bool sent = tls_send(&conn, (const u8*) request, (u16) strlen_(request));
    vga_print("tls_send_ok=0x");
    serial_print("tls_send_ok=0x");
    print_hex((u64) sent);

    u8 response[512];
    u16 response_len = 0;
    bool received = false;
    if (sent) {
        received = tls_receive(&conn, &response[0], (u16) sizeof(response), 3000, &response_len);
    }
    vga_print(" tls_receive_ok=0x");
    serial_print(" tls_receive_ok=0x");
    print_hex((u64) received);
    vga_print(" response_len=0x");
    serial_print(" response_len=0x");
    print_hex((u64) response_len);

    bool got_http_status = response_len >= 4
        && response[0] == 'H' && response[1] == 'T' && response[2] == 'T' && response[3] == 'P';
    vga_print(" got_http_status=0x");
    serial_print(" got_http_status=0x");
    print_hex((u64) got_http_status);
    vga_print("\n");
    serial_print("\n");

    tls_close(&conn);
}
