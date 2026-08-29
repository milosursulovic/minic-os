// PCI enumeration via legacy config mechanism #1 (CONFIG_ADDRESS/CONFIG_DATA,
// ports 0xCF8/0xCFC). Scoped to bus 0 only, no PCI-to-PCI bridge recursion.

#include "pci.h"
#include "../io/io.h"

static const u16 PCI_CONFIG_ADDRESS = 0xCF8;
static const u16 PCI_CONFIG_DATA = 0xCFC;

// offset must be 4-byte aligned; config space is dword-addressed.
u32 pci_config_read_dword(u8 bus, u8 device, u8 function, u8 offset) {
    u32 address = (((u32) 1) << 31)
        | (((u32) bus) << 16)
        | (((u32) device) << 11)
        | (((u32) function) << 8)
        | ((u32) (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

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

void pci_config_write_dword(u8 bus, u8 device, u8 function, u8 offset, u32 value) {
    u32 address = (((u32) 1) << 31)
        | (((u32) bus) << 16)
        | (((u32) device) << 11)
        | (((u32) function) << 8)
        | ((u32) (offset & 0xFC));
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

// Masks off the low 4 flag bits of BAR0 to get the physical base address.
// Assumes the BAR is already assigned (true under QEMU/SeaBIOS); no size probing.
u32 pci_read_bar0(u8 bus, u8 device, u8 function) {
    u32 bar0 = pci_config_read_dword(bus, device, function, 0x10);
    return bar0 & ~((u32) 0xF);
}

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

// No separate "present" bit - vendor ID 0xFFFF means no device.
static void pci_check_device(u8 bus, u8 device) {
    u16 vendor_id = pci_config_read_word(bus, device, 0, 0x00);
    if (vendor_id == 0xFFFF) {
        return;
    }
    pci_record_device(bus, device, 0);

    // Bit 7 marks a multi-function device; only then check functions 1-7.
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
