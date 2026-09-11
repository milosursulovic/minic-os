#include "hardware.h"
#include "net.h"
#include "../../../kernel/drivers/io/io.h"
#include "../../../kernel/lib/strings.h"
#include "../../../kernel/drivers/pci/pci.h"
#include "../../../kernel/drivers/device_manager/device_manager.h"
#include "../../../kernel/net/e1000/e1000.h"
#include "../../../kernel/drivers/usb/usb_hid.h"

void cmd_pci(void) {
    pci_enumerate();
    vga_print("pci devices: 0x");
    serial_print("pci devices: 0x");
    print_hex((u64) g_pci_device_count);
    int i = 0;
    while (i < g_pci_device_count) {
        pci_device* d = &g_pci_devices[i];
        vga_print(" ");
        serial_print(" ");
        print_hex((u64) d->bus);
        vga_print(":");
        serial_print(":");
        print_hex((u64) d->device);
        vga_print(".");
        serial_print(".");
        print_hex((u64) d->function);
        vga_print(" vendor=0x");
        serial_print(" vendor=0x");
        print_hex((u64) d->vendor_id);
        vga_print(" device=0x");
        serial_print(" device=0x");
        print_hex((u64) d->device_id);
        vga_print(" class=0x");
        serial_print(" class=0x");
        print_hex((u64) d->class_code);
        vga_print(" subclass=0x");
        serial_print(" subclass=0x");
        print_hex((u64) d->subclass);
        i = i + 1;
    }
}

// Real Device Manager registry (kernel/drivers/device_manager/) - a
// device appears here the moment its driver actually initializes, not
// before, since every driver in this kernel is lazily initialized on
// demand from a shell command (mouse/pci/fb/nic), not eagerly at boot.
void cmd_devices(void) {
    int i = 0;
    int shown = 0;
    while (i < MAX_DEVICES) {
        char name[32];
        int category;
        u32 info;
        if (device_manager_get(i, name, &category, &info)) {
            vga_print(name);
            serial_print(name);
            if (category == DEVICE_CATEGORY_PCI) {
                vga_print(" [PCI]");
                serial_print(" [PCI]");
            } else if (category == DEVICE_CATEGORY_PLATFORM) {
                vga_print(" [PLATFORM]");
                serial_print(" [PLATFORM]");
            } else if (category == DEVICE_CATEGORY_INPUT) {
                vga_print(" [INPUT]");
                serial_print(" [INPUT]");
            }
            vga_print(" info=0x");
            serial_print(" info=0x");
            print_hex((u64) info);
            vga_print("  ");
            serial_print("  ");
            shown = shown + 1;
        }
        i = i + 1;
    }
    if (shown == 0) {
        vga_print("(no devices registered yet)");
        serial_print("(no devices registered yet)");
    }
}

void cmd_nic(void) {
    bool ok = e1000_init();
    if (!ok) {
        vga_print("e1000 init failed - device not found at 0:3.0");
        serial_print("e1000 init failed - device not found at 0:3.0");
        return;
    }
    u8 mac[6];
    e1000_get_mac(&mac[0]);
    vga_print("e1000 mac=");
    serial_print("e1000 mac=");
    print_mac(&mac[0]);
    bool link_up = e1000_link_up();
    vga_print(" link_up=0x");
    serial_print(" link_up=0x");
    print_hex((u64) link_up);
}

// Real, hand-checkable window into kernel/drivers/usb/usb_hid.c's own
// live, polled state - same role cmd_mouse (shell/shell/commands/gui.c)
// plays for the PS/2 driver. Deliberately doesn't re-run uhci_init()/
// usb_hid_init() - those already ran once at boot (kmain.c); this just
// reports whatever they found.
void cmd_usbinfo(void) {
    vga_print("usb mouse present=0x");
    serial_print("usb mouse present=0x");
    print_hex((u64) g_usb_mouse_present);
    vga_print(" x=0x");
    serial_print(" x=0x");
    print_hex((u64) g_usb_mouse_x);
    vga_print(" y=0x");
    serial_print(" y=0x");
    print_hex((u64) g_usb_mouse_y);
    vga_print(" buttons=0x");
    serial_print(" buttons=0x");
    print_hex((u64) g_usb_mouse_buttons);
    vga_print(" reports=0x");
    serial_print(" reports=0x");
    print_hex((u64) g_usb_mouse_report_count);
    vga_print("  usb keyboard present=0x");
    serial_print("  usb keyboard present=0x");
    print_hex((u64) g_usb_keyboard_present);
    vga_print(" modifiers=0x");
    serial_print(" modifiers=0x");
    print_hex((u64) g_usb_key_modifiers);
    vga_print(" keycodes=0x");
    serial_print(" keycodes=0x");
    int i = 0;
    while (i < 6) {
        print_hex((u64) g_usb_last_keycodes[i]);
        vga_print(" ");
        serial_print(" ");
        i = i + 1;
    }
    vga_print("events=0x");
    serial_print("events=0x");
    print_hex((u64) g_usb_key_event_count);
}
