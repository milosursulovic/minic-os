// Shared config-descriptor walk - see usb_descriptor.h. Extracted
// verbatim from uhci.c's own uhci_enumerate_port() (pure refactor, zero
// behavior change) so kernel/drivers/usb/xhci.c can reuse the identical
// parse instead of a second, drifting copy.

#include "usb_descriptor.h"

bool parse_hid_endpoint(const u8* config_buf, int len, u8* endpoint_out, u8* protocol_out) {
    u8 pending_protocol = 0;
    u8 found_endpoint = 0;
    u8 found_protocol = 0;
    int pos = 0;
    while (pos + 1 < len) {
        u8 desc_len = config_buf[pos];
        u8 desc_type = config_buf[pos + 1];
        if (desc_len == 0) {
            break;
        }
        if (desc_type == 0x04 && pos + 7 < len) {  // INTERFACE descriptor
            pending_protocol = config_buf[pos + 7];  // bInterfaceProtocol
        } else if (desc_type == 0x05 && pos + 6 < len) {  // ENDPOINT descriptor
            u8 ep_addr = config_buf[pos + 2];
            u8 ep_attr = config_buf[pos + 3];
            if ((ep_addr & 0x80) != 0 && (ep_attr & 0x03) == 0x03 && found_endpoint == 0) {
                found_endpoint = (u8) (ep_addr & 0x0F);
                found_protocol = pending_protocol;
            }
        }
        pos = pos + desc_len;
    }
    if (found_endpoint == 0) {
        return false;
    }
    *endpoint_out = found_endpoint;
    *protocol_out = found_protocol;
    return true;
}
