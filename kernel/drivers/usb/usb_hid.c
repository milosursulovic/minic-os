// Real boot-protocol USB HID glue - see usb_hid.h for the design story.

#include "usb_hid.h"
#include "../device_manager/device_manager.h"
#include "usbhc.h"

volatile i32 g_usb_mouse_x;
volatile i32 g_usb_mouse_y;
volatile u8 g_usb_mouse_buttons;
volatile u32 g_usb_mouse_report_count;
volatile bool g_usb_mouse_present;

volatile u8 g_usb_key_modifiers;
volatile u8 g_usb_last_keycodes[6];
volatile u32 g_usb_key_event_count;
volatile bool g_usb_keyboard_present;

static usbhc_device_info g_mouse_dev;
static usbhc_device_info g_kbd_dev;
static u8 g_mouse_buf[8];
static u8 g_kbd_buf[8];

// Classifies purely by the real bInterfaceProtocol HID boot-protocol
// value (1=keyboard, 2=mouse) - real hardware and QEMU's own emulated
// usb-mouse/usb-kbd both report this correctly *once attached directly
// to a root-hub port*. Real gotcha found and worth remembering: QEMU
// defaults a USB input device to usb_version=2, which - on this UHCI-
// only (USB 1.1) controller - makes QEMU silently insert an extra
// "QEMU USB Hub" between the root port and the device instead of
// attaching it directly, so what this driver enumerates on that root
// port is the *hub* (bDeviceClass=9), not the real device at all. Fixed
// on the QEMU command line, not by kernel-side hub support: attach both
// devices with explicit `port=N,usb_version=1` (see kernel-qemu-test's
// own updated recipe) - confirmed via QEMU's own `info usb` monitor
// command that this puts both devices directly on root ports 1/2 with
// no hub in between.
static void classify_and_arm(usbhc_device_info* info) {
    bool as_mouse = info->interface_protocol == 2;
    bool as_keyboard = info->interface_protocol == 1;
    if (as_mouse && !g_usb_mouse_present) {
        g_mouse_dev = *info;
        g_usb_mouse_x = 400;  // same starting point PS/2 mouse.c already uses
        g_usb_mouse_y = 300;
        g_usb_mouse_present = true;
        usbhc_arm_periodic(0, &g_mouse_dev, g_mouse_buf);
        device_manager_register("USB Mouse", DEVICE_CATEGORY_INPUT, (u32) info->vendor_id << 16 | info->product_id);
    } else if (as_keyboard && !g_usb_keyboard_present) {
        g_kbd_dev = *info;
        g_usb_keyboard_present = true;
        usbhc_arm_periodic(1, &g_kbd_dev, g_kbd_buf);
        device_manager_register("USB Keyboard", DEVICE_CATEGORY_INPUT, (u32) info->vendor_id << 16 | info->product_id);
    }
}

void usb_hid_init(void) {
    // Real enumeration across BOTH controllers (kernel/drivers/usb/usbhc.c) -
    // keeps trying ports until a mouse AND a keyboard are both found, or
    // every port on every present controller has been tried.
    usbhc_device_info info;
    while ((!g_usb_mouse_present || !g_usb_keyboard_present) && usbhc_enumerate_next(&info)) {
        classify_and_arm(&info);
    }
}

void usb_hid_poll(void) {
    // uhci_poll_periodic() re-arms each slot on its own the instant it
    // sees a transfer go inactive (success or error) - nothing more to
    // do here than parse whatever real data actually came back.
    if (g_usb_mouse_present) {
        u32 len;
        if (usbhc_poll_periodic(0, &len) && len >= 3) {
            u8 buttons = g_mouse_buf[0];
            i32 dx = (i32) (i8) g_mouse_buf[1];
            i32 dy = (i32) (i8) g_mouse_buf[2];
            g_usb_mouse_x = g_usb_mouse_x + dx;
            g_usb_mouse_y = g_usb_mouse_y + dy;
            g_usb_mouse_buttons = (u8) (buttons & 0x07);
            g_usb_mouse_report_count = g_usb_mouse_report_count + 1;
        }
    }
    if (g_usb_keyboard_present) {
        u32 len;
        if (usbhc_poll_periodic(1, &len) && len >= 1) {
            g_usb_key_modifiers = g_kbd_buf[0];
            int i = 0;
            while (i < 6) {
                g_usb_last_keycodes[i] = (u32) (2 + i) < len ? g_kbd_buf[2 + i] : 0;
                i = i + 1;
            }
            g_usb_key_event_count = g_usb_key_event_count + 1;
        }
    }
}
