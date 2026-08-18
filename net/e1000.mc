// Milestone 30 (Phase X's second step): a real driver for the Intel
// e1000 (82540EM) gigabit NIC milestone 29's PCI enumeration already
// found present at 0:3.0 (vendor 0x8086, device 0x100e) - QEMU's
// default machine attaches one with zero extra flags needed.
//
// Deliberately scoped to INITIALIZATION ONLY: enable the device over
// PCI, map its memory-mapped register file, and read back real hardware
// state (its actual MAC address, its link-up status) - proving this
// kernel can genuinely talk to the device at all before building
// anything on top. Setting up the RX/TX descriptor rings and sending or
// receiving an actual packet is real DMA-buffer-management work, a
// separably-sized hard problem of its own - deferred to a later
// milestone, the same "narrowest safe first version" discipline
// milestone 29 used for PCI enumeration (bus 0 only, no bridge
// recursion) and milestone 16 used for the ATA driver (primary bus,
// master drive only).
//
// Unlike every earlier driver in this kernel (VGA, serial, keyboard,
// PIT/PIC, ATA, and PCI config space itself), the e1000's real register
// file isn't reached through port I/O at all - it's memory-mapped
// (MMIO): PCI BAR0 gives a real PHYSICAL address that has to be mapped
// into this kernel's own virtual address space via mm/paging.mc's
// map_page() before any register can be touched, then read/written as
// ordinary memory. This is the first time this kernel has ever mapped
// a device's registers this way - the heap and the `map` demo command
// mapped RAM, not hardware.

import "../drivers/pci.mc";
import "../mm/paging.mc";

// The e1000 device milestone 29 already found and confirmed present -
// a fixed, documented convention, the same class of "known demo value"
// this kernel already relies on elsewhere (File's hardcoded path,
// the ring3 channel's fixed index).
const u8 e1000_bus = 0;
const u8 e1000_device = 3;
const u8 e1000_function = 0;

// Register offsets into the MMIO BAR - the standard Intel 8254x/e1000
// family register map, publicly documented in Intel's own datasheet.
// Only what this milestone's read-only proof needs; TX/RX descriptor
// registers (RDBAL/TDBAL/etc.) are real but left for the next milestone.
const u32 e1000_reg_status = 0x0008;
const u32 e1000_reg_ral0 = 0x5400;
const u32 e1000_reg_rah0 = 0x5404;

// STATUS register bit 1 = Link Up.
const u32 e1000_status_lu = 0x2;

// map_page() works one 4KB page at a time - the registers this driver
// touches go up through RAH0 at offset 0x5404, so 8 pages (32KB) is
// real headroom past that, not exactly enough - matching every other
// fixed-size choice this kernel makes (g_loaded_image_buf, HANDLES_PER_
// PROCESS, etc: generous, not tight).
const u64 e1000_mmio_vaddr = 0x60000000;
const u64 e1000_mmio_pages = 8;

u64 g_e1000_mmio_base;

u32 e1000_read_reg(u32 offset) {
    volatile u32* reg = (volatile u32*) (g_e1000_mmio_base + (u64) offset);
    return *reg;
}

// PCI command register (offset 0x04): bit 1 = Memory Space Enable (the
// device won't even answer to MMIO reads/writes at its BAR until this
// is set - real hardware ignores accesses to a BAR whose enable bit
// isn't on), bit 2 = Bus Master Enable (required for the device to ever
// initiate a DMA transfer itself - not exercised yet in this
// milestone's read-only proof, but harmless and necessary groundwork
// for the descriptor-ring milestone that follows this one).
const u8 pci_reg_command = 0x04;
const u32 pci_command_memory_space = 0x2;
const u32 pci_command_bus_master = 0x4;

// Initializes the e1000: enables it over PCI, maps its MMIO register
// file, and reads back real hardware state. Returns false if the
// expected device isn't actually at E1000_BUS/DEVICE/FUNCTION (a real
// possibility if this ever runs against a different QEMU machine
// config, or real hardware with a different NIC) rather than silently
// mapping garbage.
bool e1000_init() {
    u16 vendor_id = pci_config_read_word(e1000_bus, e1000_device, e1000_function, 0x00);
    u16 device_id = pci_config_read_word(e1000_bus, e1000_device, e1000_function, 0x02);
    if (vendor_id != 0x8086 || device_id != 0x100e) {
        return false;
    }

    u32 command = pci_config_read_dword(e1000_bus, e1000_device, e1000_function, pci_reg_command);
    command = command | pci_command_memory_space | pci_command_bus_master;
    pci_config_write_dword(e1000_bus, e1000_device, e1000_function, pci_reg_command, command);

    u32 mmio_phys = pci_read_bar0(e1000_bus, e1000_device, e1000_function);
    if (mmio_phys == 0) {
        return false;
    }

    u64 page = 0;
    while (page < e1000_mmio_pages) {
        u64 vaddr = e1000_mmio_vaddr + (page * 4096);
        u64 paddr = ((u64) mmio_phys) + (page * 4096);
        // Device registers, not RAM - writable and non-executable
        // (PAGE_NX, milestone 28), no user bit (kernel-only, never
        // touched from ring3).
        if (!map_page(vaddr, paddr, 0x02 | page_nx)) {
            return false;
        }
        page = page + 1;
    }
    g_e1000_mmio_base = e1000_mmio_vaddr;
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
    u32 low = e1000_read_reg(e1000_reg_ral0);
    u32 high = e1000_read_reg(e1000_reg_rah0);
    mac_out[0] = (u8) (low & 0xFF);
    mac_out[1] = (u8) ((low >> 8) & 0xFF);
    mac_out[2] = (u8) ((low >> 16) & 0xFF);
    mac_out[3] = (u8) ((low >> 24) & 0xFF);
    mac_out[4] = (u8) (high & 0xFF);
    mac_out[5] = (u8) ((high >> 8) & 0xFF);
}

bool e1000_link_up() {
    u32 status = e1000_read_reg(e1000_reg_status);
    return (status & e1000_status_lu) != 0;
}

void e1000_write_reg(u32 offset, u32 value) {
    volatile u32* reg = (volatile u32*) (g_e1000_mmio_base + (u64) offset);
    *reg = value;
}

// Milestone 31: real TX/RX descriptor rings and a genuine, verified
// packet round trip - the DMA-buffer-management work milestone 30
// deliberately deferred. Everything below builds directly on milestone
// 30's own groundwork (MMIO already mapped, PCI bus-mastering already
// enabled) rather than starting over.
//
// A NIC doesn't move bytes through registers one at a time the way port
// I/O devices do - the driver hands the hardware a ring of DESCRIPTORS
// (each one a pointer to a real data buffer, plus length/status/command
// fields) living in ordinary RAM, and the device DMAs to/from those
// buffers itself once bus-mastering is enabled. `packed struct` (real
// since the freestanding phase) guarantees these match the hardware's
// own 16-byte layout exactly, no compiler-inserted padding.
packed struct tx_descriptor {
    u64 buffer_addr;
    u16 length;
    u8 cso;
    u8 cmd;
    u8 status;
    u8 css;
    u16 special;
}

packed struct rx_descriptor {
    u64 buffer_addr;
    u16 length;
    u16 checksum;
    u8 status;
    u8 errors;
    u16 special;
}

const u32 e1000_reg_tctl = 0x0400;
const u32 e1000_reg_tipg = 0x0410;
const u32 e1000_reg_tdbal = 0x3800;
const u32 e1000_reg_tdbah = 0x3804;
const u32 e1000_reg_tdlen = 0x3808;
const u32 e1000_reg_tdh = 0x3810;
const u32 e1000_reg_tdt = 0x3818;

const u32 e1000_reg_rctl = 0x0100;
const u32 e1000_reg_rdbal = 0x2800;
const u32 e1000_reg_rdbah = 0x2804;
const u32 e1000_reg_rdlen = 0x2808;
const u32 e1000_reg_rdh = 0x2810;
const u32 e1000_reg_rdt = 0x2818;

// TX descriptor CMD bits: EOP (end of packet), IFCS (hardware computes
// and appends the real Ethernet CRC), RS (report status - without this
// the hardware never sets DD, and polling for completion would spin
// forever). STATUS bit 0 = DD (descriptor done) - set by the hardware
// itself once it has genuinely finished transmitting, not something
// software can fake.
const u8 tx_cmd_eop = 0x01;
const u8 tx_cmd_ifcs = 0x02;
const u8 tx_cmd_rs = 0x08;
const u8 tx_status_dd = 0x01;

// TCTL: EN (enable) | PSP (pad short packets - our test frames are well
// under the 60-byte Ethernet minimum) | CT=15 (collision threshold,
// bits 11:4) | COLD=64 (collision distance for full duplex, bits 21:12)
// | RTLC (retransmit on late collision, bit 24) - the standard
// full-duplex configuration documented in Intel's own datasheet.
const u32 e1000_tctl_value = 0x014000FA;
// TIPG: IPGT=10 (bits 9:0) | IPGR1=8 (bits 19:10) | IPGR2=6 (bits
// 29:20) - the datasheet's own recommended full-duplex spacing.
const u32 e1000_tipg_value = 0x0060200A;

// RCTL: EN (enable) | BAM (broadcast accept - harmless here, real
// replies are unicast to us) | SECRC (strip the CRC before writing to
// memory, so `length` reflects the real payload) - BSIZE left at its
// default (00 = 2048 bytes/descriptor), comfortably under the full 4KB
// frame each RX buffer actually gets (alloc_frame()'s own granularity) -
// hardware never writes more than BSIZE, so the larger real allocation
// is just harmless headroom, not a mismatch.
const u32 e1000_rctl_value = 0x0400800A;

const u32 tx_ring_size = 8;
const u32 rx_ring_size = 8;

// One 4KB frame comfortably holds either ring (8 * 16 = 128 bytes) -
// real headroom, not exactly enough, the same sizing philosophy every
// other fixed-size table in this kernel uses.
tx_descriptor* g_tx_ring;
u32 g_tx_tail;
u8* g_tx_buffer;

rx_descriptor* g_rx_ring;
u8* g_rx_buffers[8];   // must match RX_RING_SIZE - MiniC array sizes need a literal, not a const

// Sets up both rings and enables the transmitter/receiver. Must run
// after e1000_init() - needs g_e1000_mmio_base already mapped and PCI
// bus-mastering already enabled, both milestone 30's own job.
bool e1000_init_rings() {
    void* tx_ring_frame = alloc_frame();
    void* tx_buf_frame = alloc_frame();
    if (tx_ring_frame == null || tx_buf_frame == null) {
        return false;
    }
    g_tx_ring = (tx_descriptor*) tx_ring_frame;
    g_tx_buffer = (u8*) tx_buf_frame;
    u32 i = 0;
    while (i < tx_ring_size) {
        g_tx_ring[i].buffer_addr = 0;
        g_tx_ring[i].length = 0;
        g_tx_ring[i].cmd = 0;
        g_tx_ring[i].status = tx_status_dd;   // every unused slot starts "done"
        i = i + 1;
    }
    g_tx_tail = 0;

    e1000_write_reg(e1000_reg_tdbal, (u32) ((u64) tx_ring_frame));
    e1000_write_reg(e1000_reg_tdbah, 0);
    e1000_write_reg(e1000_reg_tdlen, tx_ring_size * 16);
    e1000_write_reg(e1000_reg_tdh, 0);
    e1000_write_reg(e1000_reg_tdt, 0);
    e1000_write_reg(e1000_reg_tipg, e1000_tipg_value);
    e1000_write_reg(e1000_reg_tctl, e1000_tctl_value);

    void* rx_ring_frame = alloc_frame();
    if (rx_ring_frame == null) {
        return false;
    }
    g_rx_ring = (rx_descriptor*) rx_ring_frame;
    i = 0;
    while (i < rx_ring_size) {
        void* buf = alloc_frame();
        if (buf == null) {
            return false;
        }
        g_rx_buffers[i] = (u8*) buf;
        g_rx_ring[i].buffer_addr = (u64) buf;
        g_rx_ring[i].length = 0;
        g_rx_ring[i].status = 0;
        i = i + 1;
    }

    e1000_write_reg(e1000_reg_rdbal, (u32) ((u64) rx_ring_frame));
    e1000_write_reg(e1000_reg_rdbah, 0);
    e1000_write_reg(e1000_reg_rdlen, rx_ring_size * 16);
    e1000_write_reg(e1000_reg_rdh, 0);
    // Every descriptor is immediately available to hardware, so the
    // tail sits one past the last one - the same "head chases tail"
    // convention as the TX ring, just starting from the opposite end.
    e1000_write_reg(e1000_reg_rdt, rx_ring_size - 1);
    e1000_write_reg(e1000_reg_rctl, e1000_rctl_value);

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
    g_tx_ring[slot].cmd = tx_cmd_eop | tx_cmd_ifcs | tx_cmd_rs;
    g_tx_ring[slot].status = 0;

    g_tx_tail = (slot + 1) % tx_ring_size;
    e1000_write_reg(e1000_reg_tdt, g_tx_tail);

    u32 spins = 0;
    while (spins < 1000000) {
        if ((g_tx_ring[slot].status & tx_status_dd) != 0) {
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
// tick-based timeout instead - see shell.mc's cmd_arp().
u32 g_rx_head;

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
            e1000_write_reg(e1000_reg_rdt, g_rx_head);
            g_rx_head = (g_rx_head + 1) % rx_ring_size;
            return len;
        }
        spins = spins + 1;
    }
    return 0;
}
