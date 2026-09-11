#include "io_port_range.h"

io_port_range g_io_port_ranges[IO_PORT_RANGE_SLOTS];

int io_port_range_create(u16 port_start, u16 port_end) {
    int i = 0;
    while (i < IO_PORT_RANGE_SLOTS) {
        if (!g_io_port_ranges[i].used) {
            g_io_port_ranges[i].used = true;
            g_io_port_ranges[i].port_start = port_start;
            g_io_port_ranges[i].port_end = port_end;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

bool io_port_range_contains(int slot, u16 port) {
    if (slot < 0 || slot >= IO_PORT_RANGE_SLOTS || !g_io_port_ranges[slot].used) {
        return false;
    }
    return port >= g_io_port_ranges[slot].port_start && port <= g_io_port_ranges[slot].port_end;
}
