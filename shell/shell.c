// The minimal interactive shell, built on keyboard.c's line buffer. The
// main loop (kmain.c's _start) dispatches a line via run_command() once
// keyboard.c's IRQ1 handler (isr.c) sets g_line_ready.
//
// Grows a `cmd_*` + dispatch branch per subsystem as each one gets
// ported (mm, sched, syscall/proc/net, disk) - kept minimal here through
// Stage 1 (interrupts/keyboard skeleton only).

#include "shell.h"
#include "../drivers/io.h"
#include "../drivers/keyboard.h"
#include "../lib/strings.h"

void print_prompt(void) {
    vga_print("> ");
    serial_print("> ");
}

static void cmd_help(void) {
    vga_print("commands: help clear echo <text>");
    serial_print("commands: help clear echo <text>\n");
}

static void cmd_clear(void) {
    int i = 80;  // leave the boot message on row 0
    while (i < 2000) {
        g_vga[i].character = ' ';
        g_vga[i].color = 0x07;
        i = i + 1;
    }
    g_vga_cursor = 80;
}

static void cmd_echo(void) {
    char* text = &g_line_buffer[5];  // past "echo "
    vga_print(text);
    serial_print(text);
}

void run_command(void) {
    if (streq(g_line_buffer, "help")) {
        cmd_help();
    } else if (streq(g_line_buffer, "clear")) {
        cmd_clear();
    } else if (starts_with(g_line_buffer, "echo ")) {
        cmd_echo();
    } else if (g_line_len > 0) {
        vga_print("unknown command");
        serial_print("unknown command\n");
    }
}
