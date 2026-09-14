// USB host-controller dispatch - see usbhc.h. Plain if/else-on-a-tag
// idiom (this codebase has no function-pointer dispatch anywhere -
// kernel/fs/vfs/vfs.c's own BACKEND_MINIFS/BACKEND_DEVICE/etc chain is
// the established precedent, followed exactly here and in
// kernel/fs/blockdev/blockdev.c).

#include "usbhc.h"
#include "uhci.h"
#include "xhci.h"

static bool g_uhci_present;
static bool g_xhci_present;
static int g_uhci_port_cursor = 1;
static int g_xhci_port_cursor = 1;
static bool g_slot_is_xhci[2];

void usbhc_init(void) {
    g_uhci_present = uhci_init();
    g_xhci_present = xhci_init();
    g_uhci_port_cursor = 1;
    g_xhci_port_cursor = 1;
}

static void fill_from(usbhc_device_info* out, const usb_device_info* info, bool is_xhci) {
    out->valid = info->valid;
    out->vendor_id = info->vendor_id;
    out->product_id = info->product_id;
    out->device_class = info->device_class;
    out->interface_protocol = info->interface_protocol;
    out->endpoint = info->endpoint;
    out->max_packet_size = info->max_packet_size;
    out->handle = info->address;
    out->is_xhci = is_xhci;
}

bool usbhc_enumerate_next(usbhc_device_info* out) {
    while (g_uhci_present && g_uhci_port_cursor <= 2) {
        int port = g_uhci_port_cursor;
        g_uhci_port_cursor = g_uhci_port_cursor + 1;
        usb_device_info info;
        if (uhci_enumerate_port(port, (u8) port, &info) && info.valid) {
            fill_from(out, &info, false);
            return true;
        }
    }
    while (g_xhci_present && g_xhci_port_cursor <= xhci_port_count()) {
        int port = g_xhci_port_cursor;
        g_xhci_port_cursor = g_xhci_port_cursor + 1;
        usb_device_info info;
        if (xhci_enumerate_port(port, &info) && info.valid) {
            fill_from(out, &info, true);
            return true;
        }
    }
    return false;
}

void usbhc_arm_periodic(int slot, const usbhc_device_info* dev, u8* buf) {
    if (slot < 0 || slot >= 2) {
        return;
    }
    g_slot_is_xhci[slot] = dev->is_xhci;
    if (dev->is_xhci) {
        usb_device_info info;
        info.valid = true;
        info.vendor_id = dev->vendor_id;
        info.product_id = dev->product_id;
        info.device_class = dev->device_class;
        info.address = dev->handle;
        info.interface_protocol = dev->interface_protocol;
        info.endpoint = dev->endpoint;
        info.max_packet_size = dev->max_packet_size;
        xhci_arm_periodic(slot, &info, buf);
    } else {
        uhci_arm_periodic(slot, dev->handle, dev->endpoint, dev->max_packet_size, buf);
    }
}

bool usbhc_poll_periodic(int slot, u32* actual_len) {
    if (slot < 0 || slot >= 2) {
        return false;
    }
    if (g_slot_is_xhci[slot]) {
        return xhci_poll_periodic(slot, actual_len);
    }
    return uhci_poll_periodic(slot, actual_len);
}
