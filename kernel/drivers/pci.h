#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

typedef struct {
    u8 bus;
    u8 device;
    u8 function;
    u16 vendor_id;
    u16 device_id;
    u8 class_code;
    u8 subclass;
    u8 prog_if;
    u8 header_type;
} pci_device;

extern pci_device g_pci_devices[16];
extern int g_pci_device_count;

u32 pci_config_read_dword(u8 bus, u8 device, u8 function, u8 offset);
u16 pci_config_read_word(u8 bus, u8 device, u8 function, u8 offset);
u8 pci_config_read_byte(u8 bus, u8 device, u8 function, u8 offset);
void pci_config_write_dword(u8 bus, u8 device, u8 function, u8 offset, u32 value);
u32 pci_read_bar0(u8 bus, u8 device, u8 function);
void pci_enumerate(void);

#pragma GCC visibility pop
