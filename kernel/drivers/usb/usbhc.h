#pragma once

#include "../../../types.h"
#include "usb_descriptor.h"

#pragma GCC visibility push(hidden)

// USB host-controller dispatch layer - real-hardware driver arc item
// 4/5. usb_hid.c no longer knows or cares whether a device is on the
// real UHCI controller (kernel/drivers/usb/uhci.c) or the real xHCI
// controller (kernel/drivers/usb/xhci.c) - both may be present at once
// (QEMU's default machine always has a PIIX3 UHCI; a real 2022+ laptop
// only ever has xHCI). Unlike kernel/fs/blockdev's fixed tag->backend
// mapping, USB device-to-controller assignment is discovered
// dynamically per-port during enumeration.

typedef struct {
    bool valid;
    u16 vendor_id;
    u16 product_id;
    u8 device_class;
    u8 interface_protocol;  // 1=keyboard, 2=mouse (HID boot protocol)
    u8 endpoint;
    u8 max_packet_size;
    u8 handle;    // UHCI: device_addr. xHCI: real hardware Slot ID. Opaque to usb_hid.c.
    bool is_xhci;
} usbhc_device_info;

// Probes both controllers - uhci_init() then xhci_init() - either, both,
// or neither may succeed; usbhc_enumerate_next() only ever looks at
// whichever ones actually came up.
void usbhc_init(void);

// Returns the next not-yet-tried port across BOTH controllers (UHCI's
// fixed 2 root ports first, addresses 1/2 - identical to this driver's
// pre-xHCI behavior; then xHCI's 1..MaxPorts) with a device actually
// enumerated on it. False once every port on every present controller
// has been tried.
bool usbhc_enumerate_next(usbhc_device_info* out);

// slot: 0=mouse, 1=keyboard - usb_hid.c's own fixed two-device
// convention, unchanged from before this item.
void usbhc_arm_periodic(int slot, const usbhc_device_info* dev, u8* buf);
bool usbhc_poll_periodic(int slot, u32* actual_len);

#pragma GCC visibility pop
