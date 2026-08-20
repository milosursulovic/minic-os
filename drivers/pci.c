// Milestone 29 (Phase X's first step - "driver framework (PCI
// enumeration) + networking"): the first time this kernel discovers its
// own hardware instead of everything being a fixed, hardcoded I/O port
// (the ATA driver's 0x1F0, the PIT's 0x40, etc). Every real driver added
// from here on - and specifically the NIC driver networking needs -
// starts by finding its device here rather than assuming a fixed
// location, the same shift every real OS makes once it grows past a
// handful of hand-picked legacy ports.
//
// Uses the legacy "configuration mechanism #1" (CONFIG_ADDRESS/
// CONFIG_DATA, ports 0xCF8/0xCFC) - the original PCI config access
// method, universally supported by every real and emulated x86
// chipset including QEMU's, and the natural starting point since it's
// plain port I/O (outl/inl) with no memory-mapped I/O or ACPI table
// parsing needed first.
//
// Deliberately scoped to bus 0 only, not a full recursive walk through
// every PCI-to-PCI bridge's secondary bus - the same "narrowest safe
// first version" discipline the ATA driver used (primary bus, master
// drive only, milestone 16). QEMU's default machine has no such
// bridges; walking through one would need reading a bridge's own
// secondary-bus-number register and recursing, a real but separate
// extension for whenever a device beyond bus 0 actually needs finding.

#include "pci.h"
#include "io.h"

static const u16 PCI_CONFIG_ADDRESS = 0xCF8;
static const u16 PCI_CONFIG_DATA = 0xCFC;

// Every PCI config register access goes through this: write the
// (bus, device, function, register) address as one 32-bit dword to
// CONFIG_ADDRESS (bit 31 is the "enable" bit every real chipset
// requires set), then read the 32-bit result back from CONFIG_DATA.
// `offset` must be 4-byte aligned - real config space is dword-
// addressed even though individual fields are 1/2/4 bytes wide.
u32 pci_config_read_dword(u8 bus, u8 device, u8 function, u8 offset) {
    u32 address = (((u32) 1) << 31)
        | (((u32) bus) << 16)
        | (((u32) device) << 11)
        | (((u32) function) << 8)
        | ((u32) (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

// Real config fields are often 8 or 16 bits (vendor ID, class code) -
// these two shift/mask the containing dword rather than needing a
// second port-I/O round trip per field.
u16 pci_config_read_word(u8 bus, u8 device, u8 function, u8 offset) {
    u32 dword = pci_config_read_dword(bus, device, function, offset);
    u32 shift = ((u32) (offset & 2)) * 8;
    return (u16) ((dword >> shift) & 0xFFFF);
}

u8 pci_config_read_byte(u8 bus, u8 device, u8 function, u8 offset) {
    u32 dword = pci_config_read_dword(bus, device, function, offset);
    u32 shift = ((u32) (offset & 3)) * 8;
    return (u8) ((dword >> shift) & 0xFF);
}

// Milestone 30: the first PCI config space WRITE this kernel has ever
// done - every prior use (milestone 29's enumeration) was read-only.
// Needed to set the command register's bus-mastering bit before a NIC
// can DMA descriptor rings to/from memory. Same addressing as the read
// side; CONFIG_DATA becomes a write target instead of a read source.
void pci_config_write_dword(u8 bus, u8 device, u8 function, u8 offset, u32 value) {
    u32 address = (((u32) 1) << 31)
        | (((u32) bus) << 16)
        | (((u32) device) << 11)
        | (((u32) function) << 8)
        | ((u32) (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

// Reads BAR0 (offset 0x10) and masks off the low 4 bits (bit 0 = space
// type, bits 2:1 = 32/64-bit, bit 3 = prefetchable for a memory BAR) to
// get the actual physical base address. Deliberately NOT probing the
// region's size via the standard "write all 1s, read back the size
// mask" dance - this driver already knows its target device's layout
// from the datasheet, and only needs the address, not to rediscover the
// size. Assumes the BAR is already validly assigned, which is true here
// specifically because QEMU's `-kernel` boot still runs SeaBIOS first
// (multiboot loading is one of SeaBIOS's OWN jobs, not a BIOS bypass) -
// real PCI bus enumeration and BAR assignment already happened before
// this kernel ever got control, the same reason milestone 29's own
// vendor/device ID reads already came back real and sane.
u32 pci_read_bar0(u8 bus, u8 device, u8 function) {
    u32 bar0 = pci_config_read_dword(bus, device, function, 0x10);
    return bar0 & ~((u32) 0xF);
}

// A fixed, small cap - same class of "generous enough for today, not
// infinite" choice every fixed-size table in this kernel already makes
// (g_objects[8], g_channels[4], etc). QEMU's default machine populates a
// handful of devices (host bridge, ISA bridge, IDE controller, VGA,
// usually a default NIC) - 16 is real headroom over that, not exactly
// enough.
pci_device g_pci_devices[16];
int g_pci_device_count;

static void pci_record_device(u8 bus, u8 device, u8 function) {
    if (g_pci_device_count >= 16) {
        return;
    }
    u16 vendor_id = pci_config_read_word(bus, device, function, 0x00);
    u16 device_id = pci_config_read_word(bus, device, function, 0x02);
    u8 class_code = pci_config_read_byte(bus, device, function, 0x0B);
    u8 subclass = pci_config_read_byte(bus, device, function, 0x0A);
    u8 prog_if = pci_config_read_byte(bus, device, function, 0x09);
    u8 header_type = pci_config_read_byte(bus, device, function, 0x0E);

    int i = g_pci_device_count;
    g_pci_devices[i].bus = bus;
    g_pci_devices[i].device = device;
    g_pci_devices[i].function = function;
    g_pci_devices[i].vendor_id = vendor_id;
    g_pci_devices[i].device_id = device_id;
    g_pci_devices[i].class_code = class_code;
    g_pci_devices[i].subclass = subclass;
    g_pci_devices[i].prog_if = prog_if;
    g_pci_devices[i].header_type = header_type;
    g_pci_device_count = g_pci_device_count + 1;
}

// A device with no function ever present reads back vendor ID 0xFFFF -
// there's no separate "present" bit, this sentinel IS the presence
// check, the same convention every real PCI enumerator (and this
// kernel's own -1-sentinel style elsewhere) relies on.
static void pci_check_device(u8 bus, u8 device) {
    u16 vendor_id = pci_config_read_word(bus, device, 0, 0x00);
    if (vendor_id == 0xFFFF) {
        return;
    }
    pci_record_device(bus, device, 0);

    // Bit 7 of the header type byte marks a multi-function device -
    // only worth checking functions 1-7 at all when it's set, since a
    // single-function device's other seven "functions" aren't real
    // hardware, just unpopulated config space that may or may not
    // reliably read back 0xFFFF depending on the chipset.
    u8 header_type = pci_config_read_byte(bus, device, 0, 0x0E);
    if ((header_type & 0x80) != 0) {
        u8 function = 1;
        while (function < 8) {
            u16 fn_vendor_id = pci_config_read_word(bus, device, function, 0x00);
            if (fn_vendor_id != 0xFFFF) {
                pci_record_device(bus, device, function);
            }
            function = function + 1;
        }
    }
}

void pci_enumerate(void) {
    g_pci_device_count = 0;
    u16 device = 0;
    while (device < 32) {
        pci_check_device(0, (u8) device);
        device = device + 1;
    }
}
