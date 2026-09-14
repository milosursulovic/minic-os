#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Shared USB device-enumeration result - filled identically by
// kernel/drivers/usb/uhci.c and kernel/drivers/usb/xhci.c's own
// enumerate-port sequences (both real host-controller drivers, sharing
// this shape and the descriptor-walk below rather than each defining
// their own).
typedef struct {
    bool valid;
    u16 vendor_id;
    u16 product_id;
    u8 device_class;
    u8 address;
    u8 interface_protocol;  // 1=keyboard, 2=mouse (HID boot protocol), 0=unknown
    u8 endpoint;             // interrupt IN endpoint number
    u8 max_packet_size;      // of the interrupt endpoint (report size cap)
} usb_device_info;

// Walks a raw USB configuration descriptor (config + interface +
// endpoint descriptors back to back, as returned by a real
// GET_DESCRIPTOR(CONFIGURATION) request) looking for the first
// interrupt-IN endpoint and its interface's bInterfaceProtocol - the
// exact walk kernel/drivers/usb/uhci.c's own uhci_enumerate_port()
// used to do inline, now shared verbatim with xhci.c since both real
// host-controller drivers need the identical parse. Returns true and
// fills *endpoint_out/*protocol_out if an interrupt-IN endpoint was
// found; false (leaving both untouched) otherwise.
bool parse_hid_endpoint(const u8* config_buf, int len, u8* endpoint_out, u8* protocol_out);

#pragma GCC visibility pop
