#pragma once

#include "../../../types.h"
#include "usb_descriptor.h"

#pragma GCC visibility push(hidden)

// Real, hand-written xHCI (USB3-class host controller, spec 1.2) driver
// - real-hardware driver arc item 4/5. See xhci.c's own top comment for
// the design story and documented scope limits (polling not IRQ-driven,
// Low/Full-Speed devices only - no SuperSpeed, boot-protocol HID only,
// same limits kernel/drivers/usb/uhci.c already set for the simpler
// controller). Two fixed device slots (mouse/keyboard), same
// PERIODIC_SLOTS=2 scope uhci.c already has.

// Finds the controller via PCI (class 0x0C, subclass 0x03, prog_if
// 0x30), resets it, brings up the Command/Event rings, starts it.
// Returns false if no xHCI controller exists or bring-up failed - a
// real, honest "nothing to do", matching every other driver's own
// lazy/optional bring-up convention.
bool xhci_init(void);

// How many real root-hub ports this controller reported (HCSPARAMS1
// MaxPorts) - kernel/drivers/usb/usbhc.c iterates 1..this when probing
// for devices, the xHCI equivalent of uhci.c's own fixed two ports.
int xhci_port_count(void);

// Full per-port enumeration (reset, Enable Slot, Address Device,
// GET_DESCRIPTOR x2/x3, SET_CONFIGURATION, Configure Endpoint) - see
// xhci.c. out->address carries the real hardware-assigned Slot ID
// (xHCI's own handle for every later operation on this device, not a
// software-chosen USB address the way uhci.c's own out->address is).
// Returns false if no device is attached on that port, it's a
// SuperSpeed port (documented scope limit), or any step fails.
bool xhci_enumerate_port(int port, usb_device_info* out);

// Arms a periodic Interrupt-IN transfer for the given slot's HID
// endpoint (slot 0=mouse, 1=keyboard - usbhc.c's own fixed two-slot
// convention, mirroring uhci.c's). buf must stay valid/stable memory.
void xhci_arm_periodic(int slot, const usb_device_info* dev, u8* buf);
// True + fills *actual_len if the given slot's endpoint received a new
// report since the last poll. Internally drains any newly-available
// Event Ring entries (shared across both slots) and re-arms the
// completed endpoint's ring on its own - the caller never needs to
// call xhci_arm_periodic() again itself, same contract as
// uhci_poll_periodic().
bool xhci_poll_periodic(int slot, u32* actual_len);

#pragma GCC visibility pop
