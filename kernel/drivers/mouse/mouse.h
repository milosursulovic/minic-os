#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

extern volatile i32 g_mouse_x;
extern volatile i32 g_mouse_y;
extern volatile u8 g_mouse_buttons;  // bit0=left, bit1=right, bit2=middle
extern volatile u32 g_mouse_packet_count;
extern u32 g_mouse_raw_byte_count;
// True once mouse_init()'s real IntelliMouse negotiation (three magic
// 0xF3 sample-rate commands + an 0xF2 device-ID re-read) confirms the
// mouse actually reports a scroll wheel - not assumed, checked.
extern bool g_mouse_has_wheel;

// Talks to the PS/2 controller directly - real init, no probing/guessing.
void mouse_init(void);
// Called from isr.c's IRQ12 handler with each raw byte off port 0x60.
void mouse_handle_byte(u8 byte);
// Reads and zeroes the accumulated wheel movement since the last call
// (positive = scrolled up) - always 0 if g_mouse_has_wheel is false.
i32 mouse_take_wheel_delta(void);

#pragma GCC visibility pop
