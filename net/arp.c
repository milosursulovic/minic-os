// ARP resolver (client only, no responder) with a fixed-size cache and no eviction/TTL.
// Our own IP (net/ip.c's g_my_ip) is a fixed static assumption, not DHCP-negotiated.

#include "arp.h"
#include "e1000.h"
#include "ip.h"
#include "../isr/isr.h"

typedef struct {
    bool used;
    u8 ip[4];
    u8 mac[6];
} arp_entry;

#define ARP_CACHE_SIZE 8
static arp_entry g_arp_cache[ARP_CACHE_SIZE];
static u32 g_arp_cache_count;

static bool g_arp_nic_ready;

// Lazily brings the NIC up exactly once.
static bool arp_init(void) {
    if (g_arp_nic_ready) {
        return true;
    }
    if (!e1000_init()) {
        return false;
    }
    if (!e1000_init_rings()) {
        return false;
    }
    g_arp_nic_ready = true;
    return true;
}

bool ip_equals(u8* a, u8* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static bool arp_cache_lookup(u8* ip, u8* mac_out) {
    u32 i = 0;
    while (i < g_arp_cache_count) {
        if (g_arp_cache[i].used && ip_equals(&g_arp_cache[i].ip[0], ip)) {
            u32 j = 0;
            while (j < 6) {
                mac_out[j] = g_arp_cache[i].mac[j];
                j = j + 1;
            }
            return true;
        }
        i = i + 1;
    }
    return false;
}

static void arp_cache_insert(u8* ip, u8* mac) {
    if (g_arp_cache_count >= ARP_CACHE_SIZE) {
        return;
    }
    u32 slot = g_arp_cache_count;
    g_arp_cache[slot].used = true;
    u32 j = 0;
    while (j < 4) {
        g_arp_cache[slot].ip[j] = ip[j];
        j = j + 1;
    }
    j = 0;
    while (j < 6) {
        g_arp_cache[slot].mac[j] = mac[j];
        j = j + 1;
    }
    g_arp_cache_count = g_arp_cache_count + 1;
}

// Builds and sends one Ethernet+ARP request frame for target_ip.
// ip_init() is idempotent, called here to guarantee g_my_ip is valid.
static void arp_send_request(u8* target_ip) {
    u8 mac[6];
    e1000_get_mac(&mac[0]);

    u8 frame[60];
    int i = 0;
    while (i < 60) {
        frame[i] = 0;
        i = i + 1;
    }
    i = 0;
    while (i < 6) {
        frame[i] = 0xFF;
        i = i + 1;
    }
    i = 0;
    while (i < 6) {
        frame[6 + i] = mac[i];
        i = i + 1;
    }
    frame[12] = 0x08;
    frame[13] = 0x06;
    frame[14] = 0x00;
    frame[15] = 0x01;
    frame[16] = 0x08;
    frame[17] = 0x00;
    frame[18] = 6;
    frame[19] = 4;
    frame[20] = 0x00;
    frame[21] = 0x01;
    i = 0;
    while (i < 6) {
        frame[22 + i] = mac[i];
        i = i + 1;
    }
    ip_init();
    i = 0;
    while (i < 4) {
        frame[28 + i] = g_my_ip[i];
        i = i + 1;
    }
    i = 0;
    while (i < 4) {
        frame[38 + i] = target_ip[i];
        i = i + 1;
    }

    e1000_send(&frame[0], 60);
}

// Cache hit returns immediately; a miss sends a request and polls for a
// matching reply against a tick-based (not instruction-count) timeout.
static const u64 ARP_TIMEOUT_TICKS = 2000;

bool arp_resolve(u8* target_ip, u8* mac_out) {
    if (arp_cache_lookup(target_ip, mac_out)) {
        return true;
    }
    if (!arp_init()) {
        return false;
    }

    arp_send_request(target_ip);

    u8 reply[64];
    u64 start_tick = g_tick_count;
    while (g_tick_count - start_tick < ARP_TIMEOUT_TICKS) {
        u16 len = e1000_receive(&reply[0], 64);
        if (len > 0) {
            bool is_arp = reply[12] == 0x08 && reply[13] == 0x06;
            bool is_reply_op = reply[20] == 0x00 && reply[21] == 0x02;
            if (is_arp && is_reply_op && ip_equals(&reply[28], target_ip)) {
                u32 j = 0;
                while (j < 6) {
                    mac_out[j] = reply[22 + j];
                    j = j + 1;
                }
                arp_cache_insert(target_ip, mac_out);
                return true;
            }
        }
    }
    return false;
}
