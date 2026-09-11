#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real, hand-written UHCI (USB 1.1 host controller) driver - Faza I
// point 9, item 16. See kernel/drivers/usb/uhci.c's own top comment for
// the design story and documented scope limits (polling not IRQ-driven,
// one flat QH chain, boot-protocol HID only).

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

// Finds the controller via PCI (class 0x0C, subclass 0x03), resets it,
// builds the frame list + fixed QH chain, starts it. Returns false if no
// UHCI controller exists (a real, honest "nothing to do" - matches every
// other driver's own lazy/optional bring-up convention) or reset failed.
bool uhci_init(void);

// Real root-hub port handling + full enumeration sequence (GET_DESCRIPTOR
// x2, SET_ADDRESS, GET_DESCRIPTOR(config), SET_CONFIGURATION - see uhci.c).
// port is 1 or 2 (UHCI's own two root-hub ports). Returns false if no
// device is attached on that port, or any step of enumeration fails.
bool uhci_enumerate_port(int port, u8 new_address, usb_device_info* out);

// Real control transfer (SETUP -> DATA(s) -> STATUS), polled to
// completion (bounded spin). Returns actual bytes transferred, or -1 on
// error/timeout.
int uhci_control_transfer(u8 device_addr, u8 max_packet_size, const u8 setup[8],
                            u8* data, u16 data_len, bool data_is_in);

// Arms a periodic Interrupt-IN transfer on the given device/endpoint's
// own periodic queue slot (0=mouse, 1=keyboard - this driver's fixed
// two-slot periodic chain) and remembers the parameters so
// uhci_poll_periodic() can keep re-arming it on its own. Real DMA, not a
// copy - buf must stay valid/stable memory (a static buffer, never a
// stack one).
void uhci_arm_periodic(int slot, u8 device_addr, u8 endpoint, u8 max_packet_size, u8* buf);
// True + fills *actual_len if the given periodic slot's transfer
// completed with real data since the last poll (Active bit clear, no
// error). Internally re-arms the SAME slot (toggling data-toggle) the
// instant it finds the TD inactive, for either outcome (real data or an
// error) - the caller never needs to call uhci_arm_periodic() again
// itself. False while the TD is still in flight (don't touch it).
bool uhci_poll_periodic(int slot, u32* actual_len);

#pragma GCC visibility pop
