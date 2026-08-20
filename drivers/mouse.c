// PS/2 mouse, same 8042 controller the keyboard already sits on (aux
// device, IRQ12/vector 44 vs. the keyboard's IRQ1/vector 33). Standard
// 3-byte packet protocol, no scroll wheel (that needs a separate
// vendor-extension init sequence - deliberately out of scope for now).

#include "mouse.h"
#include "io.h"
#include "../sched/task.h"

static const u16 PS2_DATA = 0x60;
static const u16 PS2_STATUS_CMD = 0x64;
static const u32 PS2_WAIT_LIMIT = 100000;  // bounded poll, fail-clean like ata.c's waits

volatile i32 g_mouse_x = 400;  // starts centered in the fb milestone's 800x600 space
volatile i32 g_mouse_y = 300;
volatile u8 g_mouse_buttons;
volatile u32 g_mouse_packet_count;

static u8 g_packet[3];
static int g_packet_index;

// bit1 of the status register: 1 = input buffer full, not safe to write yet.
static bool wait_input_clear(void) {
    u32 i = 0;
    while (i < PS2_WAIT_LIMIT) {
        if ((inb(PS2_STATUS_CMD) & 0x02) == 0) {
            return true;
        }
        i = i + 1;
    }
    return false;
}

// bit0 of the status register: 1 = output buffer full, a byte is ready to read.
static bool wait_output_full(void) {
    u32 i = 0;
    while (i < PS2_WAIT_LIMIT) {
        if ((inb(PS2_STATUS_CMD) & 0x01) != 0) {
            return true;
        }
        i = i + 1;
    }
    return false;
}

static void ctrl_write(u8 byte) {
    wait_input_clear();
    outb(PS2_STATUS_CMD, byte);
}

static void data_write(u8 byte) {
    wait_input_clear();
    outb(PS2_DATA, byte);
}

static u8 data_read(void) {
    wait_output_full();
    return inb(PS2_DATA);
}

// 0xD4 tells the controller the next data-port byte is for the mouse, not
// the keyboard - both share port 0x60, this is how the controller tells
// them apart on the way out.
static void mouse_write(u8 byte) {
    wait_input_clear();
    outb(PS2_STATUS_CMD, 0xD4);
    data_write(byte);
}

static bool g_mouse_initialized;

void mouse_init(void) {
    // Must run exactly once - re-enabling streaming mid-stream emits a fresh
    // report that misaligns mouse_handle_byte()'s packet framing.
    if (g_mouse_initialized) {
        return;
    }
    g_mouse_initialized = true;

    ctrl_write(0xA8);  // enable the auxiliary (mouse) port

    ctrl_write(0x20);  // read controller configuration byte
    u8 status = data_read();
    status = status | 0x02;   // enable IRQ12
    status = status & ~0x20;  // enable the mouse clock line
    ctrl_write(0x60);
    data_write(status);

    // These ACKs are still read by polling, not IRQ12 - unmasking first
    // would race the interrupt handler against data_read() for the same
    // single-byte output buffer.
    mouse_write(0xF6);  // set defaults
    data_read();         // ACK (0xFA)
    mouse_write(0xF4);  // enable data reporting
    data_read();         // ACK (0xFA)

    // Only now is it safe to let IRQ12 start reaching mouse_handle_byte().
    // pic_remap() left IRQ2 (the cascade line to the slave PIC) and IRQ12
    // masked, since nothing needed the slave PIC before now - unmask both
    // (read-modify-write so the timer/keyboard's own mask bits are untouched).
    u8 master_mask = inb(0x21);
    outb(0x21, master_mask & ~0x04);
    u8 slave_mask = inb(0xA1);
    outb(0xA1, slave_mask & ~0x10);

    // Enabling streaming emits a stray byte or two shortly after (timing
    // not precise enough to drain deterministically) - let it settle, then
    // force realignment rather than let it misframe the first real packet.
    sleep_ticks(5);
    g_packet_index = 0;
}

u32 g_mouse_raw_byte_count;

void mouse_handle_byte(u8 byte) {
    g_mouse_raw_byte_count = g_mouse_raw_byte_count + 1;
    if (g_packet_index == 0 && (byte & 0x08) == 0) {
        return;  // bit3 always set on a real first byte - resync on garbage
    }
    g_packet[g_packet_index] = byte;
    g_packet_index = g_packet_index + 1;
    if (g_packet_index < 3) {
        return;
    }
    g_packet_index = 0;

    u8 flags = g_packet[0];
    i32 dx = (i32) g_packet[1];
    i32 dy = (i32) g_packet[2];
    if ((flags & 0x10) != 0) {
        dx = dx - 256;
    }
    if ((flags & 0x20) != 0) {
        dy = dy - 256;
    }

    g_mouse_x = g_mouse_x + dx;
    g_mouse_y = g_mouse_y - dy;  // PS/2's Y axis is inverted vs. screen coordinates
    if (g_mouse_x < 0) {
        g_mouse_x = 0;
    }
    if (g_mouse_x > 799) {
        g_mouse_x = 799;
    }
    if (g_mouse_y < 0) {
        g_mouse_y = 0;
    }
    if (g_mouse_y > 599) {
        g_mouse_y = 599;
    }

    g_mouse_buttons = flags & 0x07;
    g_mouse_packet_count = g_mouse_packet_count + 1;
}
