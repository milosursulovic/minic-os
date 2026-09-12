#include "system.h"
#include "../../../kernel/drivers/io/io.h"
#include "../../../kernel/drivers/keyboard/keyboard.h"
#include "../../../kernel/lib/strings.h"
#include "../../../kernel/isr/isr.h"
#include "../../../kernel/mm/heap/heap.h"

void cmd_help(void) {
    vga_print("commands: help clear ticks alloc bigalloc free free <addr> mem reset shutdown reboot cursor frame unframe frames map tasks procs ps objs netconns chan send disk diskwrite mkfs mkfile cat ls pwd cd <dir> mkdir <dir> cp <src> <dst> mv <src> <dst> touch <name> edit <name> vfscat <path> vfswrite install spawn ring3go ring3fault ring3nx ring3reg ring3unreg ring3async ring3asyncwrite ring3asyncping ring3asyncdns ring3asynctcp ring3win ring3mouse ring3text ring3button pci nic fb text mouse win winlist wincontent textcontent buttoncontent desktop arp ping <host> ipconfig dns tcp echo <text> pngtest ring3fileobj ring3perms ring3posix ring3pipe ring3shm devices exit ring3tcpserver service <start|stop|restart|status> <name> ring3widgets checkboxcontent radiocontent progresscontent slidercontent listcontent ring3focus ring3thread ring3sync ring3shmsync ring3msg ring3objs users ring3users ring3vfsperm ring3fork ring3guard ring3wait ring3posix2 ring3signfail usbinfo");
    serial_print("commands: help clear ticks alloc bigalloc free free <addr> mem reset shutdown reboot cursor frame unframe frames map tasks procs ps objs netconns chan send disk diskwrite mkfs mkfile cat ls pwd cd <dir> mkdir <dir> cp <src> <dst> mv <src> <dst> touch <name> edit <name> vfscat <path> vfswrite install spawn ring3go ring3fault ring3nx ring3reg ring3unreg ring3async ring3asyncwrite ring3asyncping ring3asyncdns ring3asynctcp ring3win ring3mouse ring3text ring3button pci nic fb text mouse win winlist wincontent textcontent buttoncontent desktop arp ping <host> ipconfig dns tcp echo <text> pngtest ring3fileobj ring3perms ring3posix ring3pipe ring3shm devices exit ring3tcpserver service <start|stop|restart|status> <name> ring3widgets checkboxcontent radiocontent progresscontent slidercontent listcontent ring3focus ring3thread ring3sync ring3shmsync ring3msg ring3objs users ring3users ring3vfsperm ring3fork ring3guard ring3wait ring3posix2 ring3signfail usbinfo");
}

void cmd_ticks(void) {
    vga_print("ticks: 0x");
    serial_print("ticks: 0x");
    print_hex(g_tick_count);
}

void cmd_clear(void) {
    int i = 80;  // leave the boot message on row 0
    while (i < 2000) {
        g_vga[i].character = ' ';
        g_vga[i].color = 0x07;
        i = i + 1;
    }
    g_vga_cursor = 80;
    vga_update_cursor(g_vga_cursor);
    term_scrollback_clear();
}

void cmd_echo(void) {
    char* text = &g_line_buffer[5];  // past "echo "
    vga_print(text);
    serial_print(text);
}

void cmd_reset(void) {
    heap_init();
    g_last_alloc = NULL;
    vga_print("heap reset");
    serial_print("heap reset");
}

// QEMU/Bochs's ACPI PM shutdown trick: writing 0x2000 to the PM1a control
// port (0x604 under QEMU's default i440fx machine) requests S5 (soft off)
// - no real ACPI table parsing exists in this kernel, this is a
// QEMU/Bochs-specific shortcut, not real-hardware ACPI. Never returns on
// QEMU; on real hardware (or a different virtual chipset) it's a no-op.
void cmd_shutdown(void) {
    vga_print("shutting down (QEMU/Bochs ACPI trick - no-op on real hardware)");
    serial_print("shutting down (QEMU/Bochs ACPI trick - no-op on real hardware)");
    outw(0x604, 0x2000);
}

// The classic 8042 keyboard-controller reset pulse - a real CPU reset,
// works on real hardware (unlike cmd_shutdown's QEMU/Bochs-only ACPI
// trick). Drains the controller's output buffer and waits for its input
// buffer to go empty before pulsing, same sequence every real OS's
// fallback reboot path uses, since writing the command byte while the
// controller is mid-transaction is unreliable.
//
// Under this kernel's usual QEMU test setup (-kernel kernel.elf, no real
// bootloader) this correctly resets the CPU, but does NOT loop back into
// minic-os: QEMU's -kernel direct-boot shortcut doesn't reload the kernel
// image on a guest-triggered reset (a general QEMU limitation, not fixable
// from guest code), so the reset lands in SeaBIOS, which then has no
// bootable device on the raw MiniFS disk.img and hangs there. The real
// GRUB-ISO boot path (./build.sh iso, VirtualBox, real hardware) has an
// actual bootloader on a bootable medium and reboots back into minic-os
// correctly.
void cmd_reboot(void) {
    vga_print("rebooting...");
    serial_print("rebooting...");
    u8 status;
    do {
        status = inb(0x64);
        if (status & 1) {
            inb(0x60);  // drain the output buffer
        }
    } while (status & 2);  // wait until the input buffer is empty
    outb(0x64, 0xFE);      // pulse the CPU reset line
    for (;;) {
        __asm__ volatile("hlt");  // fallback if the pulse is ignored
    }
}

// Reads the real hardware cursor position straight back off the CRTC
// (index 0x0E/0x0F) and compares it to g_vga_cursor - proves the two are
// actually in sync, not just "vga_update_cursor() got called somewhere".
void cmd_cursor(void) {
    outb(0x3D4, 0x0E);
    u8 hi = inb(0x3D5);
    outb(0x3D4, 0x0F);
    u8 lo = inb(0x3D5);
    int hw_pos = ((int) hi << 8) | (int) lo;
    int sw_pos = g_vga_cursor;  // captured before any printing below moves it

    vga_print("cursor hw=0x");
    serial_print("cursor hw=0x");
    print_hex((u64) hw_pos);
    vga_print(" sw=0x");
    serial_print(" sw=0x");
    print_hex((u64) sw_pos);
}
