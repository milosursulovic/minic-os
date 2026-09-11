#pragma once

#include "../../../types.h"
#include "uhci.h"

#pragma GCC visibility push(hidden)

// Real boot-protocol USB HID glue (Faza I point 9, item 16) - polled
// from kernel/isr/isr.c's existing timer tick, same place cursor/
// compositor updates already happen (no PCI IRQ routed - see uhci.c's
// own top comment). Deliberately separate globals from the PS/2 mouse
// driver's own g_mouse_x/y/buttons (kernel/drivers/mouse/mouse.h) - two
// independent input sources, never silently conflated.
extern volatile i32 g_usb_mouse_x;
extern volatile i32 g_usb_mouse_y;
extern volatile u8 g_usb_mouse_buttons;
extern volatile u32 g_usb_mouse_report_count;
extern volatile bool g_usb_mouse_present;

// Real USB HID keycodes (not PS/2 scancodes - no translation table in
// this pass, an explicit scope limit) - up to 6 simultaneously-pressed
// keys per boot-protocol report, plus the modifier byte (ctrl/shift/
// alt/gui bits).
extern volatile u8 g_usb_key_modifiers;
extern volatile u8 g_usb_last_keycodes[6];
extern volatile u32 g_usb_key_event_count;
extern volatile bool g_usb_keyboard_present;

// Enumerates whatever's actually attached on UHCI's two root-hub ports
// (uhci_init() must already have succeeded) and arms their periodic
// transfers. A port with nothing attached, or a device that isn't a
// boot-protocol mouse/keyboard, is a real, honest no-op - matches every
// other driver's own "lazily reflects what's really there" convention.
void usb_hid_init(void);
// Called once per kernel/isr/isr.c timer tick - checks both periodic
// slots for a completed transfer, parses the boot-protocol report into
// the globals above, and re-arms for the next poll.
void usb_hid_poll(void);

#pragma GCC visibility pop
