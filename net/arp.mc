// Milestone 32 (Phase X's fourth step): a real ARP resolver - the first
// protocol-layer code in this kernel, sitting on top of milestone 31's
// proven TX/RX descriptor rings. Milestone 31's own `arp` command
// deliberately crafted exactly ONE hardcoded request/reply pair purely
// as a test vehicle for RX; this file is the real thing it stood in
// for - a genuine cache plus a resolver that works for any target IP,
// not one fixed address.
//
// Deliberately scoped as a CLIENT (resolver) only, not a responder: this
// kernel's own IP address (`net/ip.mc`'s `gMyIp`, milestone 33) is
// still a fixed, static assumption, not real DHCP-negotiated
// configuration - answering "who has <our address>" for an address
// nothing actually configured doesn't mean much yet. Also no cache
// eviction/TTL (real ARP caches expire
// entries after a few minutes) - a fixed-size cache that just fills up
// is fine for a first version, the same "narrowest safe first version"
// discipline every driver-layer milestone in this phase has used.

import "e1000.mc";
import "ip.mc";
import "../isr/isr.mc";

struct ArpEntry {
    bool used;
    u8 ip[4];
    u8 mac[6];
}

// Fixed cap, matching every other table in this kernel - real headroom
// for a handful of resolved addresses, not exactly enough. MiniC array
// sizes need a literal, not a named const (hit already in milestone 31).
ArpEntry gArpCache[8];
u32 gArpCacheCount;

bool gArpNicReady;

// Lazily brings the NIC up exactly once - callers just call arpResolve()
// without needing to know or care whether milestone 30/31's own
// init/ring-setup has already run, the same "idempotent, self-
// contained" shape mm/heap.mc's heapInit() already established.
bool arpInit() {
    if (gArpNicReady) {
        return true;
    }
    if (!e1000Init()) {
        return false;
    }
    if (!e1000InitRings()) {
        return false;
    }
    gArpNicReady = true;
    return true;
}

bool ipEquals(u8* a, u8* b) {
    return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

bool arpCacheLookup(u8* ip, u8* macOut) {
    u32 i = 0;
    while (i < gArpCacheCount) {
        if (gArpCache[i].used && ipEquals(&gArpCache[i].ip[0], ip)) {
            u32 j = 0;
            while (j < 6) {
                macOut[j] = gArpCache[i].mac[j];
                j = j + 1;
            }
            return true;
        }
        i = i + 1;
    }
    return false;
}

void arpCacheInsert(u8* ip, u8* mac) {
    if (gArpCacheCount >= 8) {
        return;
    }
    u32 slot = gArpCacheCount;
    gArpCache[slot].used = true;
    u32 j = 0;
    while (j < 4) {
        gArpCache[slot].ip[j] = ip[j];
        j = j + 1;
    }
    j = 0;
    while (j < 6) {
        gArpCache[slot].mac[j] = mac[j];
        j = j + 1;
    }
    gArpCacheCount = gArpCacheCount + 1;
}

// Builds and sends one real Ethernet+ARP request frame for `targetIp` -
// the same layout milestone 31's own hardcoded version used, just
// parameterized instead of fixed to one address. Sender IP is
// `net/ip.mc`'s own `gMyIp` (milestone 33) - still a fixed, static
// assumption (10.0.2.15, QEMU SLIRP's default guest address), since this
// kernel has no real IP configuration mechanism (DHCP or otherwise) yet
// - a real gap, not hidden: see Known limitations. `ipInit()` is cheap
// and idempotent (just (re)assigns four constant bytes), so calling it
// here guarantees `gMyIp` is valid regardless of whether some other path
// (like milestone 33's own `ping`) already initialized it first.
void arpSendRequest(u8* targetIp) {
    u8 mac[6];
    e1000GetMac(&mac[0]);

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
    ipInit();
    i = 0;
    while (i < 4) {
        frame[28 + i] = gMyIp[i];
        i = i + 1;
    }
    i = 0;
    while (i < 4) {
        frame[38 + i] = targetIp[i];
        i = i + 1;
    }

    e1000Send(&frame[0], 60);
}

// The real resolver: cache hit returns immediately (no packet ever
// sent - verified in the shell command by real elapsed-tick counts, not
// just "returned the same value twice"). A miss sends one real request
// and polls for a genuinely matching reply (right EtherType, right
// opcode, right sender IP - ignoring anything else that might arrive)
// against a real wall-clock-bounded timeout using isr/isr.mc's own
// gTickCount, not a raw instruction-count spin - milestone 31 already
// found that a spin count doesn't reliably correspond to real time for
// a genuine external round trip. Returns false (a real, honest "could
// not resolve," not a hang or garbage) if nothing matching arrives
// within the budget - exercised directly by the shell's own negative
// test against an address nothing answers for.
const u64 ARP_TIMEOUT_TICKS = 2000;

bool arpResolve(u8* targetIp, u8* macOut) {
    if (arpCacheLookup(targetIp, macOut)) {
        return true;
    }
    if (!arpInit()) {
        return false;
    }

    arpSendRequest(targetIp);

    u8 reply[64];
    u64 startTick = gTickCount;
    while (gTickCount - startTick < ARP_TIMEOUT_TICKS) {
        u16 len = e1000Receive(&reply[0], 64);
        if (len > 0) {
            bool isArp = reply[12] == 0x08 && reply[13] == 0x06;
            bool isReplyOp = reply[20] == 0x00 && reply[21] == 0x02;
            if (isArp && isReplyOp && ipEquals(&reply[28], targetIp)) {
                u32 j = 0;
                while (j < 6) {
                    macOut[j] = reply[22 + j];
                    j = j + 1;
                }
                arpCacheInsert(targetIp, macOut);
                return true;
            }
            // Something else arrived (not a matching reply) - ignore it
            // and keep polling within the remaining budget.
        }
    }
    return false;
}
