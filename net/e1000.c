// Milestone 30 (Phase X's second step): a real driver for the Intel
// e1000 (82540EM) gigabit NIC milestone 29's PCI enumeration already
// found present at 0:3.0 (vendor 0x8086, device 0x100e) - QEMU's
// default machine attaches one with zero extra flags needed.
//
// Deliberately scoped to INITIALIZATION ONLY at first (enable the
// device over PCI, map its memory-mapped register file, read back its
// real MAC address and link-up status), with milestone 31's real TX/RX
// descriptor rings and a genuine, verified packet round trip added
// directly on top further down this same file - see the comment at
// e1000_init_rings() for where that begins.
//
// Unlike every earlier driver in this kernel (VGA, serial, keyboard,
// PIT/PIC, ATA, and PCI config space itself), the e1000's real register
// file isn't reached through port I/O at all - it's memory-mapped
// (MMIO): PCI BAR0 gives a real PHYSICAL address that has to be mapped
// into this kernel's own virtual address space via mm/paging.c's
// map_page() before any register can be touched, then read/written as
// ordinary memory. This is the first time this kernel has ever mapped
// a device's registers this way - the heap and the `map` demo command
// mapped RAM, not hardware.

#include "e1000.h"
#include "../drivers/pci.h"
#include "../mm/paging.h"
#include "../mm/frames.h"

// The e1000 device milestone 29 already found and confirmed present -
// a fixed, documented convention, the same class of "known demo value"
// this kernel already relies on elsewhere (File's hardcoded path,
// the ring3 channel's fixed index).
static const u8 E1000_BUS = 0;
static const u8 E1000_DEVICE = 3;
static const u8 E1000_FUNCTION = 0;

// Register offsets into the MMIO BAR - the standard Intel 8254x/e1000
// family register map, publicly documented in Intel's own datasheet.
static const u32 E1000_REG_STATUS = 0x0008;
static const u32 E1000_REG_RAL0 = 0x5400;
static const u32 E1000_REG_RAH0 = 0x5404;

// STATUS register bit 1 = Link Up.
static const u32 E1000_STATUS_LU = 0x2;

// map_page() works one 4KB page at a time - the registers this driver
// touches go up through RAH0 at offset 0x5404, so 8 pages (32KB) is
// real headroom past that, not exactly enough - matching every other
// fixed-size choice this kernel makes (g_loaded_image_buf, HANDLES_PER_
// PROCESS, etc: generous, not tight).
static const u64 E1000_MMIO_VADDR = 0x60000000;
static const u64 E1000_MMIO_PAGES = 8;

static u64 g_e1000_mmio_base;

u32 e1000_read_reg(u32 offset) {
    volatile u32* reg = (volatile u32*) (g_e1000_mmio_base + (u64) offset);
    return *reg;
}

void e1000_write_reg(u32 offset, u32 value) {
    volatile u32* reg = (volatile u32*) (g_e1000_mmio_base + (u64) offset);
    *reg = value;
}

// PCI command register (offset 0x04): bit 1 = Memory Space Enable (the
// device won't even answer to MMIO reads/writes at its BAR until this
// is set - real hardware ignores accesses to a BAR whose enable bit
// isn't on), bit 2 = Bus Master Enable (required for the device to ever
// initiate a DMA transfer itself - not exercised until e1000_init_rings()
// below, but harmless and necessary groundwork to set up-front).
static const u8 PCI_REG_COMMAND = 0x04;
static const u32 PCI_COMMAND_MEMORY_SPACE = 0x2;
static const u32 PCI_COMMAND_BUS_MASTER = 0x4;

// Initializes the e1000: enables it over PCI, maps its MMIO register
// file, and reads back real hardware state. Returns false if the
// expected device isn't actually at E1000_BUS/DEVICE/FUNCTION (a real
// possibility if this ever runs against a different QEMU machine
// config, or real hardware with a different NIC) rather than silently
// mapping garbage.
bool e1000_init(void) {
    u16 vendor_id = pci_config_read_word(E1000_BUS, E1000_DEVICE, E1000_FUNCTION, 0x00);
    u16 device_id = pci_config_read_word(E1000_BUS, E1000_DEVICE, E1000_FUNCTION, 0x02);
    if (vendor_id != 0x8086 || device_id != 0x100e) {
        return false;
    }

    u32 command = pci_config_read_dword(E1000_BUS, E1000_DEVICE, E1000_FUNCTION, PCI_REG_COMMAND);
    command = command | PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
    pci_config_write_dword(E1000_BUS, E1000_DEVICE, E1000_FUNCTION, PCI_REG_COMMAND, command);

    u32 mmio_phys = pci_read_bar0(E1000_BUS, E1000_DEVICE, E1000_FUNCTION);
    if (mmio_phys == 0) {
        return false;
    }

    u64 page = 0;
    while (page < E1000_MMIO_PAGES) {
        u64 vaddr = E1000_MMIO_VADDR + (page * 4096);
        u64 paddr = ((u64) mmio_phys) + (page * 4096);
        // Device registers, not RAM - writable and non-executable
        // (PAGE_NX), no user bit (kernel-only, never touched from ring3).
        if (!map_page(vaddr, paddr, 0x02 | PAGE_NX)) {
            return false;
        }
        page = page + 1;
    }
    g_e1000_mmio_base = E1000_MMIO_VADDR;
    return true;
}

// Reconstructs the real 48-bit MAC address from RAL0/RAH0 - the
// Receive Address registers QEMU's emulated e1000 pre-loads from its
// own (emulated) EEPROM at power-on, before any driver ever runs, the
// same way real hardware auto-loads its burned-in address. Reading
// these directly is simpler than implementing the EEPROM-read protocol
// (register EERD, a real but separate mechanism) purely to re-derive a
// value that's already sitting here in plain registers.
void e1000_get_mac(u8* mac_out) {
    u32 low = e1000_read_reg(E1000_REG_RAL0);
    u32 high = e1000_read_reg(E1000_REG_RAH0);
    mac_out[0] = (u8) (low & 0xFF);
    mac_out[1] = (u8) ((low >> 8) & 0xFF);
    mac_out[2] = (u8) ((low >> 16) & 0xFF);
    mac_out[3] = (u8) ((low >> 24) & 0xFF);
    mac_out[4] = (u8) (high & 0xFF);
    mac_out[5] = (u8) ((high >> 8) & 0xFF);
}

bool e1000_link_up(void) {
    u32 status = e1000_read_reg(E1000_REG_STATUS);
    return (status & E1000_STATUS_LU) != 0;
}

// Milestone 31: real TX/RX descriptor rings and a genuine, verified
// packet round trip - the DMA-buffer-management work e1000_init()
// deliberately deferred. Everything below builds directly on that
// groundwork (MMIO already mapped, PCI bus-mastering already enabled)
// rather than starting over.
//
// A NIC doesn't move bytes through registers one at a time the way port
// I/O devices do - the driver hands the hardware a ring of DESCRIPTORS
// (each one a pointer to a real data buffer, plus length/status/command
// fields) living in ordinary RAM, and the device DMAs to/from those
// buffers itself once bus-mastering is enabled. `__attribute__((packed))`
// guarantees these match the hardware's own 16-byte layout exactly, no
// compiler-inserted padding.
typedef struct __attribute__((packed)) {
    u64 buffer_addr;
    u16 length;
    u8 cso;
    u8 cmd;
    u8 status;
    u8 css;
    u16 special;
} tx_descriptor;

typedef struct __attribute__((packed)) {
    u64 buffer_addr;
    u16 length;
    u16 checksum;
    u8 status;
    u8 errors;
    u16 special;
} rx_descriptor;

static const u32 E1000_REG_TCTL = 0x0400;
static const u32 E1000_REG_TIPG = 0x0410;
static const u32 E1000_REG_TDBAL = 0x3800;
static const u32 E1000_REG_TDBAH = 0x3804;
static const u32 E1000_REG_TDLEN = 0x3808;
static const u32 E1000_REG_TDH = 0x3810;
static const u32 E1000_REG_TDT = 0x3818;

static const u32 E1000_REG_RCTL = 0x0100;
static const u32 E1000_REG_RDBAL = 0x2800;
static const u32 E1000_REG_RDBAH = 0x2804;
static const u32 E1000_REG_RDLEN = 0x2808;
static const u32 E1000_REG_RDH = 0x2810;
static const u32 E1000_REG_RDT = 0x2818;

// TX descriptor CMD bits: EOP (end of packet), IFCS (hardware computes
// and appends the real Ethernet CRC), RS (report status - without this
// the hardware never sets DD, and polling for completion would spin
// forever). STATUS bit 0 = DD (descriptor done) - set by the hardware
// itself once it has genuinely finished transmitting, not something
// software can fake.
static const u8 TX_CMD_EOP = 0x01;
static const u8 TX_CMD_IFCS = 0x02;
static const u8 TX_CMD_RS = 0x08;
static const u8 TX_STATUS_DD = 0x01;

// TCTL: EN (enable) | PSP (pad short packets - our test frames are well
// under the 60-byte Ethernet minimum) | CT=15 (collision threshold,
// bits 11:4) | COLD=64 (collision distance for full duplex, bits 21:12)
// | RTLC (retransmit on late collision, bit 24) - the standard
// full-duplex configuration documented in Intel's own datasheet.
static const u32 E1000_TCTL_VALUE = 0x014000FA;
// TIPG: IPGT=10 (bits 9:0) | IPGR1=8 (bits 19:10) | IPGR2=6 (bits
// 29:20) - the datasheet's own recommended full-duplex spacing.
static const u32 E1000_TIPG_VALUE = 0x0060200A;

// RCTL: EN (enable) | BAM (broadcast accept - harmless here, real
// replies are unicast to us) | SECRC (strip the CRC before writing to
// memory, so `length` reflects the real payload) - BSIZE left at its
// default (00 = 2048 bytes/descriptor), comfortably under the full 4KB
// frame each RX buffer actually gets (alloc_frame()'s own granularity) -
// hardware never writes more than BSIZE, so the larger real allocation
// is just harmless headroom, not a mismatch.
static const u32 E1000_RCTL_VALUE = 0x0400800A;

// One 4KB frame comfortably holds either ring (8 * 16 = 128 bytes) -
// real headroom, not exactly enough, the same sizing philosophy every
// other fixed-size table in this kernel uses.
static tx_descriptor* g_tx_ring;
static u32 g_tx_tail;
static u8* g_tx_buffer;

static rx_descriptor* g_rx_ring;
static u8* g_rx_buffers[RX_RING_SIZE];

// Sets up both rings and enables the transmitter/receiver. Must run
// after e1000_init() - needs g_e1000_mmio_base already mapped and PCI
// bus-mastering already enabled, both e1000_init()'s own job.
bool e1000_init_rings(void) {
    void* tx_ring_frame = alloc_frame();
    void* tx_buf_frame = alloc_frame();
    if (tx_ring_frame == NULL || tx_buf_frame == NULL) {
        return false;
    }
    g_tx_ring = (tx_descriptor*) tx_ring_frame;
    g_tx_buffer = (u8*) tx_buf_frame;
    u32 i = 0;
    while (i < TX_RING_SIZE) {
        g_tx_ring[i].buffer_addr = 0;
        g_tx_ring[i].length = 0;
        g_tx_ring[i].cmd = 0;
        g_tx_ring[i].status = TX_STATUS_DD;   // every unused slot starts "done"
        i = i + 1;
    }
    g_tx_tail = 0;

    e1000_write_reg(E1000_REG_TDBAL, (u32) ((u64) tx_ring_frame));
    e1000_write_reg(E1000_REG_TDBAH, 0);
    e1000_write_reg(E1000_REG_TDLEN, TX_RING_SIZE * 16);
    e1000_write_reg(E1000_REG_TDH, 0);
    e1000_write_reg(E1000_REG_TDT, 0);
    e1000_write_reg(E1000_REG_TIPG, E1000_TIPG_VALUE);
    e1000_write_reg(E1000_REG_TCTL, E1000_TCTL_VALUE);

    void* rx_ring_frame = alloc_frame();
    if (rx_ring_frame == NULL) {
        return false;
    }
    g_rx_ring = (rx_descriptor*) rx_ring_frame;
    i = 0;
    while (i < RX_RING_SIZE) {
        void* buf = alloc_frame();
        if (buf == NULL) {
            return false;
        }
        g_rx_buffers[i] = (u8*) buf;
        g_rx_ring[i].buffer_addr = (u64) buf;
        g_rx_ring[i].length = 0;
        g_rx_ring[i].status = 0;
        i = i + 1;
    }

    e1000_write_reg(E1000_REG_RDBAL, (u32) ((u64) rx_ring_frame));
    e1000_write_reg(E1000_REG_RDBAH, 0);
    e1000_write_reg(E1000_REG_RDLEN, RX_RING_SIZE * 16);
    e1000_write_reg(E1000_REG_RDH, 0);
    // Every descriptor is immediately available to hardware, so the
    // tail sits one past the last one - the same "head chases tail"
    // convention as the TX ring, just starting from the opposite end.
    e1000_write_reg(E1000_REG_RDT, RX_RING_SIZE - 1);
    e1000_write_reg(E1000_REG_RCTL, E1000_RCTL_VALUE);

    return true;
}

// Copies `len` bytes into the next TX slot, hands it to the hardware,
// and polls (bounded, same "fail clean rather than hang forever"
// discipline the ATA driver's ata_wait_ready/ata_wait_drq already
// established) for the descriptor's own DD bit - the hardware's own
// confirmation it genuinely completed the transmission, not something
// this driver could fake by just returning true.
bool e1000_send(u8* data, u16 len) {
    u32 i = 0;
    while (i < (u32) len) {
        g_tx_buffer[i] = data[i];
        i = i + 1;
    }

    u32 slot = g_tx_tail;
    g_tx_ring[slot].buffer_addr = (u64) g_tx_buffer;
    g_tx_ring[slot].length = len;
    g_tx_ring[slot].cmd = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
    g_tx_ring[slot].status = 0;

    g_tx_tail = (slot + 1) % TX_RING_SIZE;
    e1000_write_reg(E1000_REG_TDT, g_tx_tail);

    u32 spins = 0;
    while (spins < 1000000) {
        if ((g_tx_ring[slot].status & TX_STATUS_DD) != 0) {
            return true;
        }
        spins = spins + 1;
    }
    return false;
}

// Polls the next expected RX slot (bounded, same reasoning as
// e1000_send's own wait) for its DD bit. Returns the real received
// length and copies the frame into `out`, or 0 if nothing arrived
// within the wait window - a real, honest "no packet" result, not a
// crash or a hang. Deliberately a SHORT spin bound per call, not a long
// one - a real external reply (through QEMU's SLIRP backend) takes real
// wall-clock time to arrive, which a tight instruction-count spin loop
// doesn't reliably provide even at a huge iteration count (the loop can
// finish in microseconds on real hardware). Callers needing to wait for
// a genuine external reply should call this repeatedly against a real
// tick-based timeout instead - see net/arp.c's arp_resolve().
static u32 g_rx_head;

u16 e1000_receive(u8* out, u16 max_len) {
    u32 spins = 0;
    while (spins < 20000) {
        if ((g_rx_ring[g_rx_head].status & 0x01) != 0) {
            u16 len = g_rx_ring[g_rx_head].length;
            u16 copy_len = len;
            if (copy_len > max_len) {
                copy_len = max_len;
            }
            u16 i = 0;
            while (i < copy_len) {
                out[i] = g_rx_buffers[g_rx_head][i];
                i = i + 1;
            }
            g_rx_ring[g_rx_head].status = 0;
            e1000_write_reg(E1000_REG_RDT, g_rx_head);
            g_rx_head = (g_rx_head + 1) % RX_RING_SIZE;
            return len;
        }
        spins = spins + 1;
    }
    return 0;
}
