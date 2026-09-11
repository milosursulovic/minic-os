// Real, hand-written UHCI (USB 1.1) host controller driver (Faza I
// point 9, item 16). Real, deliberate scope limits:
//   - Polling, not IRQ-driven - no PCI IRQ line routing / new IDT vector
//     (a real, separate piece of infrastructure on its own). UHCI's own
//     spec explicitly supports polling; kernel/drivers/usb/usb_hid.c's
//     usb_hid_poll() is called from kernel/isr/isr.c's existing timer
//     tick, the same place cursor/compositor updates already happen.
//   - One flat QH chain reachable from every one of the 1024 frame-list
//     slots (mouse interrupt QH -> keyboard interrupt QH -> control QH
//     -> terminate) - every frame (1ms) services all three; real, not
//     bandwidth-optimized (no per-endpoint bInterval scheduling).
//   - Boot-protocol HID only - no SET_PROTOCOL/SET_IDLE, no HID Report
//     Descriptor parsing (kernel/drivers/usb/usb_hid.c relies on the
//     device's default report format, true for QEMU's emulated
//     usb-mouse/usb-kbd).
// Real bit-for-bit UHCI 1.1 TD/QH layout below - the part most likely to
// need a real iterative debug pass (a wrong bit can hang a transfer or
// the whole controller).

#include "uhci.h"
#include "../io/io.h"
#include "../pci/pci.h"

// ---- UHCI I/O register offsets (from the PCI BAR4 I/O base) ----
#define REG_USBCMD 0x00
#define REG_USBSTS 0x02
#define REG_USBINTR 0x04
#define REG_FRNUM 0x06
#define REG_FRBASEADD 0x08
#define REG_SOFMOD 0x0C
#define REG_PORTSC1 0x10
#define REG_PORTSC2 0x12

#define USBCMD_RS 0x0001
#define USBCMD_HCRESET 0x0002
#define USBCMD_GRESET 0x0004
#define USBCMD_CF 0x0040
#define USBCMD_MAXP 0x0080

#define USBSTS_HCHALTED 0x0020

#define PORTSC_CCS 0x0001
#define PORTSC_CSC 0x0002
#define PORTSC_PE 0x0004
#define PORTSC_PEC 0x0008
#define PORTSC_LSDA 0x0100
#define PORTSC_PR 0x0200
#define PORTSC_RESERVED1 0x0080  // bit7 always reads 1 - preserve on write

// ---- Link pointer bits (shared by TD.link, QH.link, QH.element, frame list entries) ----
#define LP_TERMINATE 0x1
#define LP_QH 0x2
#define LP_VF 0x4
#define LP_ADDR_MASK 0xFFFFFFF0u

// ---- TD dword1 (Control/Status) bits ----
#define TD_CS_ACTLEN_MASK 0x7FF
#define TD_CS_BITSTUFF (1u << 17)
#define TD_CS_CRC_TIMEOUT (1u << 18)
#define TD_CS_NAK (1u << 19)
#define TD_CS_BABBLE (1u << 20)
#define TD_CS_DBUF_ERROR (1u << 21)
#define TD_CS_STALLED (1u << 22)
#define TD_CS_ACTIVE (1u << 23)
#define TD_CS_IOC (1u << 24)
#define TD_CS_LS (1u << 26)
#define TD_CS_CERR_MASK (3u << 27)
#define TD_CS_CERR_3 (3u << 27)
#define TD_CS_SPD (1u << 29)
#define TD_CS_ERROR_MASK (TD_CS_BITSTUFF | TD_CS_CRC_TIMEOUT | TD_CS_BABBLE | TD_CS_DBUF_ERROR | TD_CS_STALLED)

// ---- TD dword2 (Token) bits ----
#define PID_SETUP 0x2D
#define PID_IN 0x69
#define PID_OUT 0xE1

typedef struct __attribute__((packed, aligned(16))) {
    u32 link;
    u32 status_control;
    u32 token;
    u32 buffer;
} uhci_td;

typedef struct __attribute__((packed, aligned(16))) {
    u32 link;
    u32 element;
} uhci_qh;

static u32 g_frame_list[1024] __attribute__((aligned(4096)));

// Periodic slots: 0 = mouse, 1 = keyboard - a fixed two-device chain
// (UHCI's own root hub only has two ports anyway).
#define PERIODIC_SLOTS 2
static uhci_qh g_periodic_qh[PERIODIC_SLOTS] __attribute__((aligned(16)));
static uhci_td g_periodic_td[PERIODIC_SLOTS] __attribute__((aligned(16)));
static u8 g_periodic_toggle[PERIODIC_SLOTS];

// Cached arm parameters, so uhci_poll_periodic() can re-arm a slot on
// its own (toggling data-toggle) the instant it sees the TD go
// inactive - the caller (kernel/drivers/usb/usb_hid.c) never re-arms a
// still-in-flight TD by accident.
typedef struct {
    bool used;
    u8 device_addr;
    u8 endpoint;
    u8 max_packet_size;
    u8* buf;
} periodic_params;
static periodic_params g_periodic_params[PERIODIC_SLOTS];

static uhci_qh g_control_qh __attribute__((aligned(16)));
#define UHCI_MAX_CONTROL_TDS 16
static uhci_td g_control_tds[UHCI_MAX_CONTROL_TDS] __attribute__((aligned(16)));

static u16 g_io_base;
static bool g_present;

static u32 phys_of(void* p) {
    return (u32) (u64) p;
}

static void reg_write16(u16 offset, u16 value) {
    outw((u16) (g_io_base + offset), value);
}

static u16 reg_read16(u16 offset) {
    return inw((u16) (g_io_base + offset));
}

static void reg_write32(u16 offset, u32 value) {
    outl((u16) (g_io_base + offset), value);
}

static bool find_uhci_controller(u8* bus_out, u8* device_out, u8* function_out) {
    pci_enumerate();
    int i = 0;
    while (i < g_pci_device_count) {
        if (g_pci_devices[i].class_code == 0x0C && g_pci_devices[i].subclass == 0x03) {
            *bus_out = g_pci_devices[i].bus;
            *device_out = g_pci_devices[i].device;
            *function_out = g_pci_devices[i].function;
            return true;
        }
        i = i + 1;
    }
    return false;
}

// Bounded busy-wait, same "fail-clean, not while(true)" convention
// kernel/fs/ata/ata.c's own waits already use - no precise millisecond
// timer needed for UHCI's own reset/settle delays.
static void bounded_spin(u32 iterations) {
    u32 i = 0;
    while (i < iterations) {
        i = i + 1;
    }
}

bool uhci_init(void) {
    u8 bus, device, function;
    if (!find_uhci_controller(&bus, &device, &function)) {
        return false;
    }
    u32 bar4 = pci_config_read_dword(bus, device, function, 0x20);
    g_io_base = (u16) (bar4 & ~0xF);

    // Global reset, then clear it - real UHCI bring-up sequence.
    reg_write16(REG_USBCMD, USBCMD_GRESET);
    bounded_spin(2000000);
    reg_write16(REG_USBCMD, 0);
    bounded_spin(200000);

    reg_write16(REG_USBINTR, 0);  // polling only - see this file's own top comment
    reg_write16(REG_FRNUM, 0);
    reg_write32(REG_FRBASEADD, phys_of(&g_frame_list[0]));
    outb((u16) (g_io_base + REG_SOFMOD), 0x40);

    // Fixed QH chain: mouse periodic -> keyboard periodic -> control -> terminate.
    // Each periodic QH starts with an empty element (T=1) until a device
    // is actually enumerated and arms it.
    g_periodic_qh[0].element = LP_TERMINATE;
    g_periodic_qh[0].link = phys_of(&g_periodic_qh[1]) | LP_QH;
    g_periodic_qh[1].element = LP_TERMINATE;
    g_periodic_qh[1].link = phys_of(&g_control_qh) | LP_QH;
    g_control_qh.element = LP_TERMINATE;
    g_control_qh.link = LP_TERMINATE;

    u32 head = phys_of(&g_periodic_qh[0]) | LP_QH;
    int i = 0;
    while (i < 1024) {
        g_frame_list[i] = head;
        i = i + 1;
    }

    reg_write16(REG_USBCMD, USBCMD_RS | USBCMD_MAXP | USBCMD_CF);
    bounded_spin(50000);

    if ((reg_read16(REG_USBSTS) & USBSTS_HCHALTED) != 0) {
        return false;  // failed to actually start - honest failure, not silently "present"
    }
    g_present = true;
    return true;
}

static u16 portsc_reg(int port) {
    return port == 1 ? REG_PORTSC1 : REG_PORTSC2;
}

static bool port_reset(int port) {
    u16 reg = portsc_reg(port);
    u16 status = reg_read16(reg);
    if ((status & PORTSC_CCS) == 0) {
        return false;  // nothing attached
    }
    // Reset: set PR, hold (real spec wants >=50ms - a bounded spin here,
    // not a precise timer, same simplification as ata.c's own waits),
    // clear it, then explicitly enable - UHCI doesn't auto-enable like
    // some later host controllers do.
    reg_write16(reg, PORTSC_RESERVED1 | PORTSC_PR);
    bounded_spin(3000000);
    reg_write16(reg, PORTSC_RESERVED1);
    bounded_spin(200000);
    status = reg_read16(reg);
    // Acknowledge connect/enable change bits (write-1-to-clear) without
    // disturbing PR/PE, then set PE.
    reg_write16(reg, PORTSC_RESERVED1 | (status & (PORTSC_CSC | PORTSC_PEC)));
    reg_write16(reg, PORTSC_RESERVED1 | PORTSC_PE);
    bounded_spin(200000);
    status = reg_read16(reg);
    return (status & PORTSC_CCS) != 0 && (status & PORTSC_PE) != 0;
}

// Builds one TD. toggle is the data-toggle bit (0 or 1); pid is
// PID_SETUP/PID_IN/PID_OUT; len is the packet length (0..max_packet_size).
static void build_td(uhci_td* td, u8 device_addr, u8 endpoint, u8 pid, u8 toggle,
                       void* buffer, u16 len, bool ioc) {
    td->link = LP_TERMINATE;
    u32 cs = TD_CS_ACTIVE | TD_CS_CERR_3;
    if (ioc) {
        cs = cs | TD_CS_IOC;
    }
    td->status_control = cs;
    u32 max_len_field = len == 0 ? 0x7FF : (u32) (len - 1);
    td->token = (u32) pid
        | ((u32) (device_addr & 0x7F) << 8)
        | ((u32) (endpoint & 0xF) << 15)
        | ((u32) (toggle & 1) << 19)
        | (max_len_field << 21);
    td->buffer = buffer == NULL ? 0 : phys_of(buffer);
}

int uhci_control_transfer(u8 device_addr, u8 max_packet_size, const u8 setup[8],
                            u8* data, u16 data_len, bool data_is_in) {
    if (max_packet_size == 0) {
        max_packet_size = 8;
    }
    int td_count = 0;
    build_td(&g_control_tds[td_count], device_addr, 0, PID_SETUP, 0, (void*) setup, 8, false);
    td_count = td_count + 1;

    u8 toggle = 1;
    u16 remaining = data_len;
    u16 offset = 0;
    u8 data_pid = data_is_in ? PID_IN : PID_OUT;
    while (remaining > 0 && td_count < UHCI_MAX_CONTROL_TDS - 1) {
        u16 chunk = remaining < max_packet_size ? remaining : max_packet_size;
        build_td(&g_control_tds[td_count], device_addr, 0, data_pid, toggle, &data[offset], chunk, false);
        toggle = (u8) (toggle ^ 1);
        offset = (u16) (offset + chunk);
        remaining = (u16) (remaining - chunk);
        td_count = td_count + 1;
    }

    // Status stage: opposite direction of the data stage (or IN if there
    // was no data stage at all), always DATA1, zero length, IOC set so
    // we can tell the whole transfer is done.
    u8 status_pid = data_len > 0 ? (data_is_in ? PID_OUT : PID_IN) : PID_IN;
    build_td(&g_control_tds[td_count], device_addr, 0, status_pid, 1, NULL, 0, true);
    int status_td_index = td_count;
    td_count = td_count + 1;

    int i = 0;
    while (i < td_count - 1) {
        g_control_tds[i].link = phys_of(&g_control_tds[i + 1]);
        i = i + 1;
    }
    g_control_tds[td_count - 1].link = LP_TERMINATE;

    g_control_qh.element = phys_of(&g_control_tds[0]);

    u32 spins = 0;
    while (spins < 2000000) {
        if ((g_control_tds[status_td_index].status_control & TD_CS_ACTIVE) == 0) {
            break;
        }
        spins = spins + 1;
    }
    if (spins >= 2000000) {
        g_control_qh.element = LP_TERMINATE;
        return -1;
    }

    // Check every TD for a real error - a stall/timeout on any stage
    // fails the whole transfer, not just the one packet.
    i = 0;
    int data_actual_total = 0;
    while (i < td_count) {
        u32 cs = g_control_tds[i].status_control;
        if ((cs & TD_CS_ERROR_MASK) != 0) {
            g_control_qh.element = LP_TERMINATE;
            return -1;
        }
        if (i >= 1 && i < status_td_index) {
            u32 raw = cs & TD_CS_ACTLEN_MASK;
            data_actual_total = data_actual_total + (raw == TD_CS_ACTLEN_MASK ? 0 : (int) raw + 1);
        }
        i = i + 1;
    }
    g_control_qh.element = LP_TERMINATE;
    return data_actual_total;
}

// Standard USB SETUP packet layout (bmRequestType, bRequest, wValue,
// wIndex, wLength - 8 bytes), built in place rather than via a shared
// struct since only this file ever constructs one.
static void build_setup(u8* setup, u8 request_type, u8 request, u16 value, u16 index, u16 length) {
    setup[0] = request_type;
    setup[1] = request;
    setup[2] = (u8) (value & 0xFF);
    setup[3] = (u8) ((value >> 8) & 0xFF);
    setup[4] = (u8) (index & 0xFF);
    setup[5] = (u8) ((index >> 8) & 0xFF);
    setup[6] = (u8) (length & 0xFF);
    setup[7] = (u8) ((length >> 8) & 0xFF);
}

#define REQ_GET_DESCRIPTOR 0x06
#define REQ_SET_ADDRESS 0x05
#define REQ_SET_CONFIGURATION 0x09
#define DESC_DEVICE 0x01
#define DESC_CONFIGURATION 0x02

bool uhci_enumerate_port(int port, u8 new_address, usb_device_info* out) {
    if (!g_present) {
        return false;
    }
    out->valid = false;
    if (!port_reset(port)) {
        return false;
    }

    u8 setup[8];
    u8 buf[64];

    // Step 1: learn bMaxPacketSize0 via an 8-byte device descriptor read
    // at the default address (0).
    build_setup(setup, 0x80, REQ_GET_DESCRIPTOR, (u16) (DESC_DEVICE << 8), 0, 8);
    if (uhci_control_transfer(0, 8, setup, buf, 8, true) < 8) {
        return false;
    }
    u8 max_packet_size = buf[7];

    // Step 2: SET_ADDRESS - real spec allows the device up to a few ms
    // to complete the change before it responds at the new address.
    build_setup(setup, 0x00, REQ_SET_ADDRESS, new_address, 0, 0);
    if (uhci_control_transfer(0, max_packet_size, setup, NULL, 0, true) < 0) {
        return false;
    }
    bounded_spin(500000);

    // Step 3: full device descriptor at the new address.
    build_setup(setup, 0x80, REQ_GET_DESCRIPTOR, (u16) (DESC_DEVICE << 8), 0, 18);
    if (uhci_control_transfer(new_address, max_packet_size, setup, buf, 18, true) < 18) {
        return false;
    }
    out->vendor_id = (u16) (buf[8] | (buf[9] << 8));
    out->product_id = (u16) (buf[10] | (buf[11] << 8));
    out->device_class = buf[4];
    out->address = new_address;
    out->max_packet_size = max_packet_size;

    // Step 4: configuration descriptor - first 9 bytes for wTotalLength,
    // then the real full descriptor set (config+interface+endpoint).
    build_setup(setup, 0x80, REQ_GET_DESCRIPTOR, (u16) (DESC_CONFIGURATION << 8), 0, 9);
    if (uhci_control_transfer(new_address, max_packet_size, setup, buf, 9, true) < 9) {
        return false;
    }
    u16 total_len = (u16) (buf[2] | (buf[3] << 8));
    if (total_len > sizeof(buf)) {
        total_len = (u16) sizeof(buf);
    }
    build_setup(setup, 0x80, REQ_GET_DESCRIPTOR, (u16) (DESC_CONFIGURATION << 8), 0, total_len);
    int got = uhci_control_transfer(new_address, max_packet_size, setup, buf, total_len, true);
    if (got < 9) {
        return false;
    }

    u8 config_value = buf[5];
    out->interface_protocol = 0;
    out->endpoint = 0;
    u8 pending_protocol = 0;
    int pos = 0;
    while (pos + 1 < got) {
        u8 desc_len = buf[pos];
        u8 desc_type = buf[pos + 1];
        if (desc_len == 0) {
            break;
        }
        if (desc_type == 0x04 && pos + 7 < got) {  // INTERFACE descriptor
            pending_protocol = buf[pos + 7];  // bInterfaceProtocol
        } else if (desc_type == 0x05 && pos + 6 < got) {  // ENDPOINT descriptor
            u8 ep_addr = buf[pos + 2];
            u8 ep_attr = buf[pos + 3];
            if ((ep_addr & 0x80) != 0 && (ep_attr & 0x03) == 0x03 && out->endpoint == 0) {
                out->endpoint = (u8) (ep_addr & 0x0F);
                out->interface_protocol = pending_protocol;
            }
        }
        pos = pos + desc_len;
    }

    build_setup(setup, 0x00, REQ_SET_CONFIGURATION, config_value, 0, 0);
    uhci_control_transfer(new_address, max_packet_size, setup, NULL, 0, true);

    out->valid = out->endpoint != 0;
    return out->valid;
}

void uhci_arm_periodic(int slot, u8 device_addr, u8 endpoint, u8 max_packet_size, u8* buf) {
    if (slot < 0 || slot >= PERIODIC_SLOTS) {
        return;
    }
    g_periodic_params[slot].used = true;
    g_periodic_params[slot].device_addr = device_addr;
    g_periodic_params[slot].endpoint = endpoint;
    g_periodic_params[slot].max_packet_size = max_packet_size;
    g_periodic_params[slot].buf = buf;
    build_td(&g_periodic_td[slot], device_addr, endpoint, PID_IN, g_periodic_toggle[slot], buf, max_packet_size, false);
    g_periodic_qh[slot].element = phys_of(&g_periodic_td[slot]);
}

static void rearm_from_cache(int slot) {
    if (!g_periodic_params[slot].used) {
        return;
    }
    build_td(&g_periodic_td[slot], g_periodic_params[slot].device_addr, g_periodic_params[slot].endpoint,
             PID_IN, g_periodic_toggle[slot], g_periodic_params[slot].buf,
             g_periodic_params[slot].max_packet_size, false);
    g_periodic_qh[slot].element = phys_of(&g_periodic_td[slot]);
}

bool uhci_poll_periodic(int slot, u32* actual_len) {
    if (slot < 0 || slot >= PERIODIC_SLOTS) {
        return false;
    }
    u32 cs = g_periodic_td[slot].status_control;
    if ((cs & TD_CS_ACTIVE) != 0) {
        // Still in flight, or the HC is retrying a plain NAK (no new
        // report yet) on its own - real UHCI behavior leaves Active set
        // across a NAK, no software intervention needed. Don't touch it.
        return false;
    }
    if ((cs & TD_CS_ERROR_MASK) != 0) {
        // A real error (after C_ERR exhausted) - real USB semantics:
        // data toggle only ever advances on a genuinely ACKed transfer,
        // never on an error, so re-arm with the SAME toggle.
        rearm_from_cache(slot);
        return false;
    }
    u32 raw = cs & TD_CS_ACTLEN_MASK;
    *actual_len = raw == TD_CS_ACTLEN_MASK ? 0 : raw + 1;
    g_periodic_toggle[slot] = (u8) (g_periodic_toggle[slot] ^ 1);
    rearm_from_cache(slot);
    return true;
}
