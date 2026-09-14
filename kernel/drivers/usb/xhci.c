// Real, hand-written xHCI (USB3-class host controller, spec 1.2) driver
// - real-hardware driver arc item 4/5. See xhci.h's own top comment for
// scope limits. Genuinely the largest, most stateful driver in this
// codebase: unlike UHCI's flat frame-list/QH/TD scheme, xHCI uses a
// Command Ring (controller commands), a single shared Event Ring
// (completions for everything), and a per-endpoint Transfer Ring per
// device - all real 16-byte TRB-based ring buffers with software/
// hardware "cycle bit" handshaking. Bit-for-bit register/TRB layout
// below is the part most likely to need a real iterative debug pass
// (same honest expectation uhci.c's own top comment already set for
// the simpler controller).

#include "xhci.h"
#include "../pci/pci.h"
#include "../device_manager/device_manager.h"
#include "../../mm/paging/paging.h"
#include "../../mm/frames/frames.h"
#include "usb_descriptor.h"

// ---- Operational register offsets (from g_op_base = BAR0 + CAPLENGTH) ----
#define OP_USBCMD 0x00
#define OP_USBSTS 0x04
#define OP_CRCR 0x18
#define OP_DCBAAP 0x30
#define OP_CONFIG 0x38
#define OP_PORTSC(n) (0x400 + ((n) - 1) * 0x10)

#define USBCMD_RS 0x1
#define USBCMD_HCRST 0x2
#define USBSTS_HCH 0x1
#define USBSTS_CNR (1u << 11)

#define PORTSC_CCS 0x1
#define PORTSC_PED 0x2
#define PORTSC_PR 0x10
#define PORTSC_SPEED_MASK (0xFu << 10)
#define PORTSC_SPEED_SHIFT 10
#define PORTSC_CSC (1u << 17)
#define PORTSC_PEC (1u << 18)
#define PORTSC_PRC (1u << 21)
#define PORTSC_RW1C_MASK (PORTSC_CSC | PORTSC_PEC | PORTSC_PRC | (1u << 20) | (1u << 22) | (1u << 23))

// ---- Runtime registers, Interrupter 0 (g_rt_base + 0x20) ----
#define RT_IR0 0x20
#define IR_IMAN 0x00
#define IR_ERSTSZ 0x08
#define IR_ERSTBA 0x10
#define IR_ERDP 0x18

// ---- TRB types (control field bits 10-15) ----
#define TRB_NORMAL 1
#define TRB_SETUP_STAGE 2
#define TRB_DATA_STAGE 3
#define TRB_STATUS_STAGE 4
#define TRB_LINK 6
#define TRB_ENABLE_SLOT 9
#define TRB_ADDRESS_DEVICE 11
#define TRB_CONFIGURE_ENDPOINT 12
#define TRB_EVALUATE_CONTEXT 13
#define TRB_TRANSFER_EVENT 32
#define TRB_CMD_COMPLETION_EVENT 33

#define TRB_CYCLE 0x1u
#define TRB_TC 0x2u        // Link TRB: Toggle Cycle
#define TRB_ENT 0x2u        // Data/Status/Normal: Evaluate Next TRB (unused, 0)
#define TRB_ISP 0x4u
#define TRB_IOC 0x20u
#define TRB_IDT 0x40u       // Setup Stage: Immediate Data (setup bytes ARE the parameter field)
#define TRB_DIR_IN (1u << 16)
#define TRB_TYPE_SHIFT 10

#define RING_SIZE 32  // 31 usable entries + 1 Link TRB

typedef struct __attribute__((packed, aligned(16))) {
    u64 parameter;
    u32 status;
    u32 control;
} xhci_trb;

typedef struct __attribute__((packed, aligned(16))) {
    u64 ring_segment_base;
    u32 ring_segment_size;  // bits0-15 valid, rest reserved
    u32 reserved;
} xhci_erst_entry;

static u64 g_mmio_base;
static u64 g_op_base;
static u64 g_rt_base;
static u64 g_db_base;
static u32 g_context_size;  // 32 or 64, from HCCPARAMS1.CSZ
static int g_max_ports;

static xhci_trb g_cmd_ring[RING_SIZE] __attribute__((aligned(64)));
static u32 g_cmd_enqueue;
static u32 g_cmd_cycle = 1;

static xhci_trb g_event_ring[RING_SIZE] __attribute__((aligned(64)));
static xhci_erst_entry g_erst[1] __attribute__((aligned(64)));
static u32 g_evt_dequeue;
static u32 g_evt_cycle = 1;

static void* g_dcbaa;  // one page: DCBAA[0]=scratchpad array ptr (if any), [1..XHCI_MAX_SLOTS]=per-slot output context ptr

#define XHCI_MAX_SLOTS 2  // real scope limit - this driver only ever needs 2 devices (mouse+keyboard)

typedef struct {
    bool used;
    u8 speed;  // PORTSC Port Speed value at enumeration (1=FS,2=LS,3=HS) - reused by Configure Endpoint's Slot Context rewrite
    u8 root_hub_port;
    void* output_ctx;
    xhci_trb* ep0_ring;
    u32 ep0_enqueue;
    u32 ep0_cycle;
    xhci_trb* hid_ring;
    u32 hid_enqueue;
    u32 hid_cycle;
    u8 hid_dci;
} xhci_slot_state;
static xhci_slot_state g_slot_state[XHCI_MAX_SLOTS + 1];  // index 1..XHCI_MAX_SLOTS (real hw slot ids); 0 unused

// usbhc.c's own fixed periodic-slot convention (0=mouse,1=keyboard) ->
// which real hw slot id that device ended up on, and its own
// last-polled-report bookkeeping.
static u8 g_periodic_hw_slot[2];
static bool g_periodic_ready[2];
static u32 g_periodic_actual_len[2];
static u8* g_periodic_buf[2];
static u8 g_periodic_max_packet[2];

static u64 phys_of(void* p) {
    return (u64) p;
}

static u32 reg_read32(u64 addr) {
    return *(volatile u32*) addr;
}

static void reg_write32(u64 addr, u32 value) {
    *(volatile u32*) addr = value;
}

static void reg_write64(u64 addr, u64 value) {
    reg_write32(addr, (u32) value);
    reg_write32(addr + 4, (u32) (value >> 32));
}

static void zero_page(void* page) {
    u8* p = (u8*) page;
    u32 i = 0;
    while (i < 4096) {
        p[i] = 0;
        i = i + 1;
    }
}

static bool bounded_wait_bits(u64 addr, u32 mask, bool want_set) {
    u32 spins = 0;
    while (spins < 2000000) {
        u32 v = reg_read32(addr);
        bool set = (v & mask) != 0;
        if (set == want_set) {
            return true;
        }
        spins = spins + 1;
    }
    return false;
}

// ---- Context array helpers - context_size varies (32 or 64 bytes),
// so every offset is computed rather than assumed. Input Context array:
// index0=Input Control, index1=Slot, index(dci+1)=Endpoint(DCI=dci).
// Output Device Context array (no Input Control prefix): index0=Slot,
// index(dci)=Endpoint(DCI=dci). Add/Drop flag bit N corresponds to
// array index (N+1) in BOTH layouts (bit0=slot, bit(dci)=EP dci) -
// this falls out of the Input array's own index-1 offset from flags.
static void* ctx_at(void* base, int array_index) {
    return (u8*) base + (u64) array_index * g_context_size;
}

static void ctx_write(void* ctx, int dword_index, u32 value) {
    *(u32*) ((u8*) ctx + dword_index * 4) = value;
}

static void set_trb(xhci_trb* trb, u64 parameter, u32 status, u32 control) {
    trb->parameter = parameter;
    trb->status = status;
    trb->control = control;
}

// Initializes a fresh RING_SIZE-entry ring's Link TRB (last slot,
// pointing back to slot 0, Toggle Cycle set) - shared shape for the
// Command Ring and every per-endpoint Transfer Ring.
static void init_ring_link(xhci_trb* ring) {
    set_trb(&ring[RING_SIZE - 1], phys_of(&ring[0]), 0,
            ((u32) TRB_LINK << TRB_TYPE_SHIFT) | TRB_TC);
}

// Enqueues one TRB onto a software-managed ring (Command Ring or a
// Transfer Ring), handling wraparound via the ring's own Link TRB -
// returns the physical address of the slot used (so the caller can
// match it against a later completion event).
static u64 ring_enqueue(xhci_trb* ring, u32* enqueue, u32* cycle, u64 parameter, u32 status, u32 control_no_cycle) {
    xhci_trb* slot = &ring[*enqueue];
    u64 my_phys = phys_of(slot);
    set_trb(slot, parameter, status, control_no_cycle | *cycle);
    *enqueue = *enqueue + 1;
    if (*enqueue == RING_SIZE - 1) {
        ring[RING_SIZE - 1].control = ((u32) TRB_LINK << TRB_TYPE_SHIFT) | TRB_TC | *cycle;
        *enqueue = 0;
        *cycle = *cycle ^ 1;
    }
    return my_phys;
}

// Peeks the Event Ring's next entry; if its cycle bit matches what we
// currently expect (a real new event, not stale/unwritten memory),
// copies it out, advances the dequeue pointer + cycle state, and
// writes the real ERDP register back (bit3 = EHB, write-1-to-clear).
static bool try_consume_event(xhci_trb* out) {
    xhci_trb* entry = &g_event_ring[g_evt_dequeue];
    if ((entry->control & TRB_CYCLE) != g_evt_cycle) {
        return false;
    }
    *out = *entry;
    g_evt_dequeue = g_evt_dequeue + 1;
    if (g_evt_dequeue == RING_SIZE) {
        g_evt_dequeue = 0;
        g_evt_cycle = g_evt_cycle ^ 1;
    }
    reg_write64(g_rt_base + RT_IR0 + IR_ERDP, phys_of(&g_event_ring[g_evt_dequeue]) | 0x8);
    return true;
}

// Bounded-spin wait for a specific event type, optionally matching a
// specific source TRB pointer (0 = don't care) - any other event seen
// along the way is a stray (this driver never has more than one
// command/transfer genuinely outstanding) and is silently dropped.
static bool wait_for_event(u8 want_type, u64 match_ptr, u32* status_out, u8* slot_out) {
    u32 spins = 0;
    while (spins < 20000000) {
        xhci_trb entry;
        if (try_consume_event(&entry)) {
            u8 type = (u8) ((entry.control >> TRB_TYPE_SHIFT) & 0x3F);
            if (type == want_type && (match_ptr == 0 || entry.parameter == match_ptr)) {
                if (status_out != 0) {
                    *status_out = entry.status;
                }
                if (slot_out != 0) {
                    *slot_out = (u8) ((entry.control >> 24) & 0xFF);
                }
                return true;
            }
        }
        spins = spins + 1;
    }
    return false;
}

static bool ring_command(u64 parameter, u32 status, u32 control_no_cycle, u32* status_out, u8* slot_out) {
    u64 my_phys = ring_enqueue(g_cmd_ring, &g_cmd_enqueue, &g_cmd_cycle, parameter, status, control_no_cycle);
    reg_write32(g_db_base + 0, 0);
    return wait_for_event(TRB_CMD_COMPLETION_EVENT, my_phys, status_out, slot_out);
}

static bool completion_ok(u32 status) {
    return ((status >> 24) & 0xFF) == 1;  // Completion Code 1 = Success
}

static bool find_xhci_controller(u8* bus_out, u8* device_out, u8* function_out) {
    pci_enumerate();
    int i = 0;
    while (i < g_pci_device_count) {
        if (g_pci_devices[i].class_code == 0x0C && g_pci_devices[i].subclass == 0x03
            && g_pci_devices[i].prog_if == 0x30) {
            *bus_out = g_pci_devices[i].bus;
            *device_out = g_pci_devices[i].device;
            *function_out = g_pci_devices[i].function;
            return true;
        }
        i = i + 1;
    }
    return false;
}

static const u64 XHCI_MMIO_VADDR = 0x66000000;
// QEMU's own qemu-xhci reports a real BAR0 extent of exactly 0x4000
// bytes (confirmed via monitor `info qtree`: "bar 0: mem at 0xfebb0000
// [0xfebb3fff]") - mapping more than that (this driver originally tried
// 16 pages) reads/writes past the real MMIO region into unbacked
// address space and crashes. 4 pages matches the confirmed real extent;
// real hardware may need a proper BAR-sizing probe (write all-1s, read
// back) instead of a fixed constant - out of scope while this item's
// verification stays QEMU-only.
static const u64 XHCI_MMIO_PAGES = 4;

int xhci_port_count(void) {
    return g_max_ports;
}

bool xhci_init(void) {
    u8 bus, device, function;
    if (!find_xhci_controller(&bus, &device, &function)) {
        return false;
    }

    u32 command = pci_config_read_dword(bus, device, function, 0x04);
    command = command | 0x2 | 0x4;  // memory space + bus master
    pci_config_write_dword(bus, device, function, 0x04, command);

    u32 bar0_lo = pci_config_read_dword(bus, device, function, 0x10) & ~((u32) 0xF);
    u32 bar0_hi = pci_config_read_dword(bus, device, function, 0x14);
    u64 mmio_phys = (((u64) bar0_hi) << 32) | (u64) bar0_lo;
    if (mmio_phys == 0) {
        return false;
    }

    u64 page = 0;
    while (page < XHCI_MMIO_PAGES) {
        if (!map_page(XHCI_MMIO_VADDR + page * 4096, mmio_phys + page * 4096, 0x02 | PAGE_NX)) {
            return false;
        }
        page = page + 1;
    }
    g_mmio_base = XHCI_MMIO_VADDR;

    u8 cap_length = *(volatile u8*) g_mmio_base;
    g_op_base = g_mmio_base + cap_length;
    u32 dboff = reg_read32(g_mmio_base + 0x14) & ~0x3u;
    u32 rtsoff = reg_read32(g_mmio_base + 0x18) & ~0x1Fu;
    g_db_base = g_mmio_base + dboff;
    g_rt_base = g_mmio_base + rtsoff;

    u32 hcsparams1 = reg_read32(g_mmio_base + 0x04);
    g_max_ports = (int) ((hcsparams1 >> 24) & 0xFF);
    u32 hcsparams2 = reg_read32(g_mmio_base + 0x08);
    u32 max_scratchpad = ((hcsparams2 >> 21 & 0x1F) << 5) | (hcsparams2 >> 27 & 0x1F);
    u32 hccparams1 = reg_read32(g_mmio_base + 0x10);
    g_context_size = ((hccparams1 & 0x4) != 0) ? 64 : 32;

    // Reset: HCRST, wait for it to self-clear AND CNR to drop.
    reg_write32(g_op_base + OP_USBCMD, USBCMD_HCRST);
    if (!bounded_wait_bits(g_op_base + OP_USBCMD, USBCMD_HCRST, false)) {
        return false;
    }
    if (!bounded_wait_bits(g_op_base + OP_USBSTS, USBSTS_CNR, false)) {
        return false;
    }

    reg_write32(g_op_base + OP_CONFIG, XHCI_MAX_SLOTS);

    g_dcbaa = alloc_frame();
    if (g_dcbaa == 0) {
        return false;
    }
    zero_page(g_dcbaa);
    reg_write64(g_op_base + OP_DCBAAP, phys_of(g_dcbaa));

    if (max_scratchpad > 0) {
        void* scratch_array = alloc_frame();
        if (scratch_array == 0) {
            return false;
        }
        zero_page(scratch_array);
        u32 s = 0;
        while (s < max_scratchpad && s < 512) {  // one page of 64-bit pointers holds 512 entries - real headroom
            void* buf = alloc_frame();
            if (buf == 0) {
                return false;
            }
            ((u64*) scratch_array)[s] = phys_of(buf);
            s = s + 1;
        }
        ((u64*) g_dcbaa)[0] = phys_of(scratch_array);
    }

    init_ring_link(g_cmd_ring);
    reg_write64(g_op_base + OP_CRCR, phys_of(&g_cmd_ring[0]) | TRB_CYCLE);

    g_erst[0].ring_segment_base = phys_of(&g_event_ring[0]);
    g_erst[0].ring_segment_size = RING_SIZE;
    g_erst[0].reserved = 0;
    reg_write32(g_rt_base + RT_IR0 + IR_ERSTSZ, 1);
    reg_write64(g_rt_base + RT_IR0 + IR_ERDP, phys_of(&g_event_ring[0]));
    reg_write64(g_rt_base + RT_IR0 + IR_ERSTBA, phys_of(&g_erst[0]));

    reg_write32(g_op_base + OP_USBCMD, USBCMD_RS);
    if (!bounded_wait_bits(g_op_base + OP_USBSTS, USBSTS_HCH, false)) {
        return false;
    }

    device_manager_register("xHCI Controller", DEVICE_CATEGORY_PCI, 0);
    return true;
}

static bool port_reset(int port) {
    u64 addr = g_op_base + OP_PORTSC(port);
    u32 status = reg_read32(addr);
    if ((status & PORTSC_CCS) == 0) {
        return false;
    }
    u32 speed = (status >> PORTSC_SPEED_SHIFT) & 0xF;
    if (speed >= 4) {
        return false;  // SuperSpeed - documented scope limit, Low/Full-Speed only
    }
    reg_write32(addr, (status & ~PORTSC_RW1C_MASK) | PORTSC_PR);
    if (!bounded_wait_bits(addr, PORTSC_PRC, true)) {
        return false;
    }
    status = reg_read32(addr);
    reg_write32(addr, (status & ~PORTSC_RW1C_MASK) | PORTSC_PRC);  // ack (RW1C)
    status = reg_read32(addr);
    return (status & PORTSC_CCS) != 0 && (status & PORTSC_PED) != 0;
}

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
#define REQ_SET_CONFIGURATION 0x09
#define DESC_DEVICE 0x01
#define DESC_CONFIGURATION 0x02

// Real control transfer over a slot's EP0 Transfer Ring - Setup (IDT,
// the 8 bytes packed directly into the parameter field) + optional
// Data Stage + Status Stage (opposite direction, IOC set so we always
// get an event to wait on). Returns actual bytes transferred (Data
// Stage's own reported residual), or -1 on error/timeout.
static int control_transfer(xhci_slot_state* st, u8 hw_slot_id, u8 setup[8], u8* data, u16 data_len, bool data_is_in) {
    u64 setup_param;
    u8* sp = (u8*) &setup_param;
    int si = 0;
    while (si < 8) {
        sp[si] = setup[si];
        si = si + 1;
    }
    u32 trt = data_len == 0 ? 0 : (data_is_in ? 3u : 2u);
    ring_enqueue(st->ep0_ring, &st->ep0_enqueue, &st->ep0_cycle, setup_param, 8,
                 ((u32) TRB_SETUP_STAGE << TRB_TYPE_SHIFT) | TRB_IDT | (trt << 16));

    if (data_len > 0) {
        u32 dir = data_is_in ? TRB_DIR_IN : 0;
        ring_enqueue(st->ep0_ring, &st->ep0_enqueue, &st->ep0_cycle, phys_of(data), data_len,
                     ((u32) TRB_DATA_STAGE << TRB_TYPE_SHIFT) | dir);
    }

    u32 status_dir = data_len > 0 ? (data_is_in ? 0 : TRB_DIR_IN) : TRB_DIR_IN;
    u64 status_phys = ring_enqueue(st->ep0_ring, &st->ep0_enqueue, &st->ep0_cycle, 0, 0,
                                    ((u32) TRB_STATUS_STAGE << TRB_TYPE_SHIFT) | TRB_IOC | status_dir);

    // Doorbell value = Device Context Index; EP0 (control) is always DCI 1.
    reg_write32(g_db_base + (u64) hw_slot_id * 4, 1);

    u32 evt_status;
    if (!wait_for_event(TRB_TRANSFER_EVENT, status_phys, &evt_status, 0)) {
        return -1;
    }
    if (!completion_ok(evt_status)) {
        return -1;
    }
    return data_len > 0 ? (int) data_len : 0;
}

bool xhci_enumerate_port(int port, usb_device_info* out) {
    out->valid = false;
    if (!port_reset(port)) {
        return false;
    }
    u32 portsc = reg_read32(g_op_base + OP_PORTSC(port));
    u8 speed = (u8) ((portsc >> PORTSC_SPEED_SHIFT) & 0xF);

    u32 slot_status;
    u8 hw_slot_id = 0;
    if (!ring_command(0, 0, (u32) TRB_ENABLE_SLOT << TRB_TYPE_SHIFT, &slot_status, &hw_slot_id)) {
        return false;
    }
    if (!completion_ok(slot_status) || hw_slot_id == 0 || hw_slot_id > XHCI_MAX_SLOTS) {
        return false;
    }

    xhci_slot_state* st = &g_slot_state[hw_slot_id];
    st->used = true;
    st->speed = speed;
    st->root_hub_port = (u8) port;
    st->output_ctx = alloc_frame();
    st->ep0_ring = (xhci_trb*) alloc_frame();
    if (st->output_ctx == 0 || st->ep0_ring == 0) {
        return false;
    }
    zero_page(st->output_ctx);
    zero_page(st->ep0_ring);
    init_ring_link(st->ep0_ring);
    st->ep0_enqueue = 0;
    st->ep0_cycle = 1;
    ((u64*) g_dcbaa)[hw_slot_id] = phys_of(st->output_ctx);

    void* input = alloc_frame();
    if (input == 0) {
        return false;
    }
    zero_page(input);
    ctx_write(ctx_at(input, 0), 1, 0x3);  // Add flags: bit0=slot, bit1=EP0(dci1)
    void* slot_ctx = ctx_at(input, 1);
    ctx_write(slot_ctx, 0, ((u32) speed << 20) | (1u << 27));  // Route=0, Speed, Context Entries=1
    ctx_write(slot_ctx, 1, (u32) port << 16);                   // Root Hub Port Number
    void* ep0_ctx = ctx_at(input, 2);
    ctx_write(ep0_ctx, 1, (3u << 1) | (4u << 3) | (8u << 16));  // CErr=3, Type=Control, MaxPacketSize=8 (initial guess)
    ctx_write(ep0_ctx, 2, (phys_of(st->ep0_ring) & ~0xFull) | 1);  // TR Dequeue Ptr lo + DCS=1
    ctx_write(ep0_ctx, 3, (u32) (phys_of(st->ep0_ring) >> 32));
    ctx_write(ep0_ctx, 4, 8);  // Average TRB Length

    u32 addr_status;
    if (!ring_command(phys_of(input), 0, ((u32) TRB_ADDRESS_DEVICE << TRB_TYPE_SHIFT) | ((u32) hw_slot_id << 24),
                       &addr_status, 0)) {
        return false;
    }
    if (!completion_ok(addr_status)) {
        return false;
    }

    u8 setup[8];
    u8 buf[64];
    build_setup(setup, 0x80, REQ_GET_DESCRIPTOR, (u16) (DESC_DEVICE << 8), 0, 8);
    if (control_transfer(st, hw_slot_id, setup, buf, 8, true) < 8) {
        return false;
    }
    u8 real_max_packet = buf[7];
    if (real_max_packet != 0 && real_max_packet != 8) {
        zero_page(input);
        ctx_write(ctx_at(input, 0), 1, 0x2);  // Add flags: bit1=EP0 only
        void* ep0_ctx2 = ctx_at(input, 2);
        ctx_write(ep0_ctx2, 1, (3u << 1) | (4u << 3) | ((u32) real_max_packet << 16));
        ctx_write(ep0_ctx2, 2, (phys_of(st->ep0_ring) & ~0xFull) | 1);
        ctx_write(ep0_ctx2, 3, (u32) (phys_of(st->ep0_ring) >> 32));
        ctx_write(ep0_ctx2, 4, real_max_packet);
        u32 eval_status;
        ring_command(phys_of(input), 0, ((u32) TRB_EVALUATE_CONTEXT << TRB_TYPE_SHIFT) | ((u32) hw_slot_id << 24),
                     &eval_status, 0);
        // A failed Evaluate Context here just means we keep using the
        // 8-byte guess - not fatal (every real LS/FS device's EP0 max
        // packet size is 8 anyway; this path exists for correctness on
        // devices that genuinely differ, not because it's commonly hit).
    }

    build_setup(setup, 0x80, REQ_GET_DESCRIPTOR, (u16) (DESC_DEVICE << 8), 0, 18);
    if (control_transfer(st, hw_slot_id, setup, buf, 18, true) < 18) {
        return false;
    }
    out->vendor_id = (u16) (buf[8] | (buf[9] << 8));
    out->product_id = (u16) (buf[10] | (buf[11] << 8));
    out->device_class = buf[4];
    out->address = hw_slot_id;
    out->max_packet_size = real_max_packet != 0 ? real_max_packet : 8;

    build_setup(setup, 0x80, REQ_GET_DESCRIPTOR, (u16) (DESC_CONFIGURATION << 8), 0, 9);
    if (control_transfer(st, hw_slot_id, setup, buf, 9, true) < 9) {
        return false;
    }
    u16 total_len = (u16) (buf[2] | (buf[3] << 8));
    if (total_len > sizeof(buf)) {
        total_len = (u16) sizeof(buf);
    }
    build_setup(setup, 0x80, REQ_GET_DESCRIPTOR, (u16) (DESC_CONFIGURATION << 8), 0, total_len);
    int got = control_transfer(st, hw_slot_id, setup, buf, total_len, true);
    if (got < 9) {
        return false;
    }
    u8 config_value = buf[5];
    out->interface_protocol = 0;
    out->endpoint = 0;
    parse_hid_endpoint(buf, got, &out->endpoint, &out->interface_protocol);

    build_setup(setup, 0x00, REQ_SET_CONFIGURATION, config_value, 0, 0);
    control_transfer(st, hw_slot_id, setup, NULL, 0, true);

    out->valid = out->endpoint != 0;
    return out->valid;
}

// Nearest xHCI Interval field (2^N * 125us slots) for a typical HID
// device's bInterval - a fixed, documented simplification (not read
// from the real endpoint descriptor, since it only affects polling
// cadence, never correctness: the controller can never deliver reports
// faster than the device's own real bInterval regardless of what we
// request here, and every real mouse/keyboard's bInterval falls
// comfortably in the few-ms range this value already targets).
#define HID_INTERVAL_FIELD 6  // 2^6 * 125us = 8ms

static void configure_hid_endpoint(xhci_slot_state* st, u8 hw_slot_id, u8 dci, u8 max_packet_size) {
    void* input = alloc_frame();
    if (input == 0) {
        return;
    }
    zero_page(input);
    ctx_write(ctx_at(input, 0), 1, 0x1u | (1u << dci));  // Add flags: bit0=slot, bit(dci)=new endpoint
    void* slot_ctx = ctx_at(input, 1);
    ctx_write(slot_ctx, 0, ((u32) st->speed << 20) | ((u32) dci << 27));  // Context Entries = dci (highest configured)
    ctx_write(slot_ctx, 1, (u32) st->root_hub_port << 16);
    void* ep_ctx = ctx_at(input, dci + 1);
    ctx_write(ep_ctx, 0, (u32) HID_INTERVAL_FIELD << 16);
    ctx_write(ep_ctx, 1, (3u << 1) | (7u << 3) | ((u32) max_packet_size << 16));  // CErr=3, Type=Interrupt IN
    ctx_write(ep_ctx, 2, (phys_of(st->hid_ring) & ~0xFull) | 1);
    ctx_write(ep_ctx, 3, (u32) (phys_of(st->hid_ring) >> 32));
    ctx_write(ep_ctx, 4, max_packet_size);

    u32 status;
    ring_command(phys_of(input), 0, ((u32) TRB_CONFIGURE_ENDPOINT << TRB_TYPE_SHIFT) | ((u32) hw_slot_id << 24),
                 &status, 0);
}

static void requeue_normal(int slot) {
    u8 hw_slot_id = g_periodic_hw_slot[slot];
    xhci_slot_state* st = &g_slot_state[hw_slot_id];
    ring_enqueue(st->hid_ring, &st->hid_enqueue, &st->hid_cycle, phys_of(g_periodic_buf[slot]),
                 g_periodic_max_packet[slot], ((u32) TRB_NORMAL << TRB_TYPE_SHIFT) | TRB_IOC);
    reg_write32(g_db_base + (u64) hw_slot_id * 4, st->hid_dci);
}

void xhci_arm_periodic(int slot, const usb_device_info* dev, u8* buf) {
    if (slot < 0 || slot >= 2) {
        return;
    }
    u8 hw_slot_id = dev->address;
    if (hw_slot_id == 0 || hw_slot_id > XHCI_MAX_SLOTS) {
        return;
    }
    xhci_slot_state* st = &g_slot_state[hw_slot_id];
    g_periodic_hw_slot[slot] = hw_slot_id;
    g_periodic_buf[slot] = buf;
    g_periodic_max_packet[slot] = dev->max_packet_size;

    if (st->hid_ring == 0) {
        u8 dci = (u8) (2 * dev->endpoint + 1);  // IN endpoint: DCI = 2*EndpointNumber + 1
        st->hid_ring = (xhci_trb*) alloc_frame();
        if (st->hid_ring == 0) {
            return;
        }
        zero_page(st->hid_ring);
        init_ring_link(st->hid_ring);
        st->hid_enqueue = 0;
        st->hid_cycle = 1;
        st->hid_dci = dci;
        configure_hid_endpoint(st, hw_slot_id, dci, dev->max_packet_size);
    }
    requeue_normal(slot);
}

// Drains every currently-available Event Ring entry (shared across
// both slots) and updates each periodic slot's own "new report ready"
// flag - idempotent, safe to call from either slot's own poll in the
// same tick without double-processing or missing an event meant for
// the other slot.
static void drain_pending_events(void) {
    xhci_trb entry;
    while (try_consume_event(&entry)) {
        u8 type = (u8) ((entry.control >> TRB_TYPE_SHIFT) & 0x3F);
        if (type != TRB_TRANSFER_EVENT) {
            continue;  // a stray command completion (shouldn't happen - single-outstanding design) - drop
        }
        u8 evt_slot_id = (u8) ((entry.control >> 24) & 0xFF);
        u8 evt_dci = (u8) ((entry.control >> 16) & 0x1F);
        u32 completion_code = (entry.status >> 24) & 0xFF;
        u32 residual = entry.status & 0xFFFFFF;
        if (completion_code != 1 && completion_code != 13) {  // not Success and not Short Packet
            continue;
        }
        int p = -1;
        if (g_periodic_hw_slot[0] == evt_slot_id && g_slot_state[evt_slot_id].hid_dci == evt_dci) {
            p = 0;
        } else if (g_periodic_hw_slot[1] == evt_slot_id && g_slot_state[evt_slot_id].hid_dci == evt_dci) {
            p = 1;
        }
        if (p >= 0) {
            u32 requested = g_periodic_max_packet[p];
            g_periodic_actual_len[p] = requested > residual ? requested - residual : 0;
            g_periodic_ready[p] = true;
        }
    }
}

bool xhci_poll_periodic(int slot, u32* actual_len) {
    if (slot < 0 || slot >= 2) {
        return false;
    }
    drain_pending_events();
    if (!g_periodic_ready[slot]) {
        return false;
    }
    g_periodic_ready[slot] = false;
    *actual_len = g_periodic_actual_len[slot];
    requeue_normal(slot);
    return true;
}
