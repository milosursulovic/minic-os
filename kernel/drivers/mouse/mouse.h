#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

extern volatile i32 g_mouse_x;
extern volatile i32 g_mouse_y;
extern volatile u8 g_mouse_buttons;  // bit0=left, bit1=right, bit2=middle
extern volatile u32 g_mouse_packet_count;
extern u32 g_mouse_raw_byte_count;

// Talks to the PS/2 controller directly - real init, no probing/guessing.
void mouse_init(void);
// Called from isr.c's IRQ12 handler with each raw byte off port 0x60.
void mouse_handle_byte(u8 byte);

#pragma GCC visibility pop
