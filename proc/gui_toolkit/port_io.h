#pragma once
#include "core.h"

// Real capability-gated port I/O (Faza I point 14, item 14) - wraps
// syscall 99. handle must be an OBJ_IO_PORT_RANGE grant covering the
// requested port (kernel/syscall/handlers/port_io.c enforces this, not
// just convention) - currently only proc/drivers/rtc_driver/rtc_driver.c
// holds one.

static __attribute__((unused)) int gt_port_inb(int handle, u16 port) {
    u64 result = gt_syscall(99, (u64) handle, 0, (u64) port);
    return result == (u64) -1 ? -1 : (int) result;
}

static __attribute__((unused)) bool gt_port_outb(int handle, u16 port, u8 value) {
    u64 packed = (u64) port | ((u64) value << 16);
    return gt_syscall(99, (u64) handle, 1, packed) != (u64) -1;
}
