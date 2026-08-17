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
// mapPage() before any register can be touched, then read/written as
// ordinary memory. This is the first time this kernel has ever mapped
// a device's registers this way - the heap and the `map` demo command
// mapped RAM, not hardware.

import "../drivers/pci.mc";
import "../mm/paging.mc";

// The e1000 device milestone 29 already found and confirmed present -
// a fixed, documented convention, the same class of "known demo value"
// this kernel already relies on elsewhere (File's hardcoded path,
// the ring3 channel's fixed index).
const u8 E1000_BUS = 0;
const u8 E1000_DEVICE = 3;
const u8 E1000_FUNCTION = 0;

// Register offsets into the MMIO BAR - the standard Intel 8254x/e1000
// family register map, publicly documented in Intel's own datasheet.
// Only what this milestone's read-only proof needs; TX/RX descriptor
// registers (RDBAL/TDBAL/etc.) are real but left for the next milestone.
const u32 E1000_REG_STATUS = 0x0008;
const u32 E1000_REG_RAL0 = 0x5400;
const u32 E1000_REG_RAH0 = 0x5404;

// STATUS register bit 1 = Link Up.
const u32 E1000_STATUS_LU = 0x2;

// mapPage() works one 4KB page at a time - the registers this driver
// touches go up through RAH0 at offset 0x5404, so 8 pages (32KB) is
// real headroom past that, not exactly enough - matching every other
// fixed-size choice this kernel makes (gLoadedImageBuf, HANDLES_PER_
// PROCESS, etc: generous, not tight).
const u64 E1000_MMIO_VADDR = 0x60000000;
const u64 E1000_MMIO_PAGES = 8;

u64 gE1000MmioBase;

u32 e1000ReadReg(u32 offset) {
    volatile u32* reg = (volatile u32*) (gE1000MmioBase + (u64) offset);
    return *reg;
}

// PCI command register (offset 0x04): bit 1 = Memory Space Enable (the
// device won't even answer to MMIO reads/writes at its BAR until this
// is set - real hardware ignores accesses to a BAR whose enable bit
// isn't on), bit 2 = Bus Master Enable (required for the device to ever
// initiate a DMA transfer itself - not exercised yet in this
// milestone's read-only proof, but harmless and necessary groundwork
// for the descriptor-ring milestone that follows this one).
const u8 PCI_REG_COMMAND = 0x04;
const u32 PCI_COMMAND_MEMORY_SPACE = 0x2;
const u32 PCI_COMMAND_BUS_MASTER = 0x4;

// Initializes the e1000: enables it over PCI, maps its MMIO register
// file, and reads back real hardware state. Returns false if the
// expected device isn't actually at E1000_BUS/DEVICE/FUNCTION (a real
// possibility if this ever runs against a different QEMU machine
// config, or real hardware with a different NIC) rather than silently
// mapping garbage.
bool e1000Init() {
    u16 vendorId = pciConfigReadWord(E1000_BUS, E1000_DEVICE, E1000_FUNCTION, 0x00);
    u16 deviceId = pciConfigReadWord(E1000_BUS, E1000_DEVICE, E1000_FUNCTION, 0x02);
    if (vendorId != 0x8086 || deviceId != 0x100e) {
        return false;
    }

    u32 command = pciConfigReadDword(E1000_BUS, E1000_DEVICE, E1000_FUNCTION, PCI_REG_COMMAND);
    command = command | PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
    pciConfigWriteDword(E1000_BUS, E1000_DEVICE, E1000_FUNCTION, PCI_REG_COMMAND, command);

    u32 mmioPhys = pciReadBar0(E1000_BUS, E1000_DEVICE, E1000_FUNCTION);
    if (mmioPhys == 0) {
        return false;
    }

    u64 page = 0;
    while (page < E1000_MMIO_PAGES) {
        u64 vaddr = E1000_MMIO_VADDR + (page * 4096);
        u64 paddr = ((u64) mmioPhys) + (page * 4096);
        // Device registers, not RAM - writable and non-executable
        // (PAGE_NX, milestone 28), no user bit (kernel-only, never
        // touched from ring3).
        if (!mapPage(vaddr, paddr, 0x02 | PAGE_NX)) {
            return false;
        }
        page = page + 1;
    }
    gE1000MmioBase = E1000_MMIO_VADDR;
    return true;
}

// Reconstructs the real 48-bit MAC address from RAL0/RAH0 - the
// Receive Address registers QEMU's emulated e1000 pre-loads from its
// own (emulated) EEPROM at power-on, before any driver ever runs, the
// same way real hardware auto-loads its burned-in address. Reading
// these directly is simpler than implementing the EEPROM-read protocol
// (register EERD, a real but separate mechanism) purely to re-derive a
// value that's already sitting here in plain registers.
void e1000GetMac(u8* macOut) {
    u32 low = e1000ReadReg(E1000_REG_RAL0);
    u32 high = e1000ReadReg(E1000_REG_RAH0);
    macOut[0] = (u8) (low & 0xFF);
    macOut[1] = (u8) ((low >> 8) & 0xFF);
    macOut[2] = (u8) ((low >> 16) & 0xFF);
    macOut[3] = (u8) ((low >> 24) & 0xFF);
    macOut[4] = (u8) (high & 0xFF);
    macOut[5] = (u8) ((high >> 8) & 0xFF);
}

bool e1000LinkUp() {
    u32 status = e1000ReadReg(E1000_REG_STATUS);
    return (status & E1000_STATUS_LU) != 0;
}
