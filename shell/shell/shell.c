// Interactive shell over keyboard.c's line buffer.

#include "shell.h"
#include "../editor/editor.h"
#include "../../kernel/drivers/io/io.h"
#include "../../kernel/drivers/keyboard/keyboard.h"
#include "../../kernel/lib/strings.h"
#include "../../kernel/mm/heap/heap.h"
#include "../../kernel/mm/frames/frames.h"
#include "../../kernel/mm/paging/paging.h"
#include "../../kernel/sched/task.h"
#include "../../proc/ipc/channel/channel.h"
#include "../../proc/ipc/pipe/pipe.h"
#include "../../kernel/isr/isr.h"
#include "../../kernel/fs/ata/ata.h"
#include "../../kernel/fs/minifs/minifs.h"
#include "../../kernel/fs/vfs/vfs.h"
#include "../../proc/process.h"
#include "../../proc/ipc/object/object.h"
#include "../../kernel/drivers/pci/pci.h"
#include "../../kernel/drivers/device_manager/device_manager.h"
#include "../../kernel/services/service_manager.h"
#include "../../kernel/net/e1000/e1000.h"
#include "../../kernel/net/arp/arp.h"
#include "../../kernel/net/ip/ip.h"
#include "../../kernel/net/icmp/icmp.h"
#include "../../kernel/net/dns/dns.h"
#include "../../kernel/net/tcp/tcp.h"
#include "../../kernel/drivers/vbe/vbe.h"
#include "../../kernel/drivers/mouse/mouse.h"
#include "../../kernel/gfx/window/window.h"
#include "../../kernel/gfx/image/image.h"
#include "../../kernel/gfx/png/png.h"

#pragma GCC visibility push(hidden)
extern u8 g_test_prog_start;
extern u8 g_test_prog_end;
#pragma GCC visibility pop

void print_prompt(void) {
    vga_print("> ");
    serial_print("> ");
}

#define HISTORY_MAX 16
static char g_history[HISTORY_MAX][128];
static int g_history_count;          // total real entries recorded, capped at HISTORY_MAX
static int g_history_next;           // ring buffer write slot
static int g_history_browse_index = -1;  // -1 = fresh line, 0 = most recent entry, 1 = one before that, ...

void shell_history_add(const char* line) {
    if (line[0] == '\0') {
        return;  // don't clutter history with a bare Enter
    }
    int i = 0;
    while (line[i] != '\0' && i < 127) {
        g_history[g_history_next][i] = line[i];
        i = i + 1;
    }
    g_history[g_history_next][i] = '\0';
    g_history_next = (g_history_next + 1) % HISTORY_MAX;
    if (g_history_count < HISTORY_MAX) {
        g_history_count = g_history_count + 1;
    }
    g_history_browse_index = -1;
}

// Erases the in-progress command line in place (real per-char backspace,
// same mirroring as isr.c's own Backspace) then retypes whatever
// g_history_browse_index now points at (or nothing, for -1).
static void shell_history_redraw(void) {
    // g_vga_cursor might be sitting mid-line (Left/Right arrow) rather than
    // at the true end of the current line - walk it out to the end first
    // (mirroring every step, so the GUI terminal's own tracked column
    // follows along), since the erase loop below assumes it's erasing
    // from there.
    while (g_line_cursor < g_line_len) {
        g_vga_cursor = g_vga_cursor + 1;
        g_line_cursor = g_line_cursor + 1;
        term_scrollback_cursor_right();
    }

    while (g_line_len > 0) {
        g_line_len = g_line_len - 1;
        g_vga_cursor = g_vga_cursor - 1;
        g_vga[g_vga_cursor].character = ' ';
        g_vga[g_vga_cursor].color = 0x0F;
        serial_putc('\b');
        serial_putc(' ');
        serial_putc('\b');
        term_scrollback_backspace();
    }
    vga_update_cursor(g_vga_cursor);

    if (g_history_browse_index >= 0) {
        int slot = ((g_history_next - 1 - g_history_browse_index) % HISTORY_MAX + HISTORY_MAX) % HISTORY_MAX;
        int i = 0;
        while (g_history[slot][i] != '\0') {
            char c = g_history[slot][i];
            g_line_buffer[g_line_len] = c;
            g_line_len = g_line_len + 1;
            vga_putc(c);
            serial_putc((u8) c);
            i = i + 1;
        }
    }
    g_line_cursor = g_line_len;  // recall always lands the cursor at the end, same as a real shell
}

void shell_history_up(void) {
    if (g_history_browse_index + 1 >= g_history_count) {
        return;  // already at the oldest entry (or history is empty)
    }
    g_history_browse_index = g_history_browse_index + 1;
    shell_history_redraw();
}

void shell_history_down(void) {
    if (g_history_browse_index < 0) {
        return;  // already on a fresh line, nothing to come back to
    }
    g_history_browse_index = g_history_browse_index - 1;
    shell_history_redraw();
}

static void cmd_help(void) {
    vga_print("commands: help clear ticks alloc bigalloc free free <addr> mem reset shutdown reboot cursor frame unframe frames map tasks procs ps objs netconns chan send disk diskwrite mkfs mkfile cat ls pwd cd <dir> mkdir <dir> cp <src> <dst> mv <src> <dst> touch <name> edit <name> vfscat <path> vfswrite install spawn ring3go ring3fault ring3nx ring3reg ring3unreg ring3async ring3asyncwrite ring3asyncping ring3asyncdns ring3asynctcp ring3win ring3mouse ring3text ring3button pci nic fb text mouse win winlist wincontent textcontent buttoncontent desktop arp ping <host> ipconfig dns tcp echo <text> pngtest ring3fileobj ring3perms ring3posix ring3pipe ring3shm devices exit ring3tcpserver service <start|stop|restart|status> <name> ring3widgets checkboxcontent radiocontent progresscontent slidercontent listcontent ring3focus");
    serial_print("commands: help clear ticks alloc bigalloc free free <addr> mem reset shutdown reboot cursor frame unframe frames map tasks procs ps objs netconns chan send disk diskwrite mkfs mkfile cat ls pwd cd <dir> mkdir <dir> cp <src> <dst> mv <src> <dst> touch <name> edit <name> vfscat <path> vfswrite install spawn ring3go ring3fault ring3nx ring3reg ring3unreg ring3async ring3asyncwrite ring3asyncping ring3asyncdns ring3asynctcp ring3win ring3mouse ring3text ring3button pci nic fb text mouse win winlist wincontent textcontent buttoncontent desktop arp ping <host> ipconfig dns tcp echo <text> pngtest ring3fileobj ring3perms ring3posix ring3pipe ring3shm devices exit ring3tcpserver service <start|stop|restart|status> <name> ring3widgets checkboxcontent radiocontent progresscontent slidercontent listcontent ring3focus");
}

static void cmd_ticks(void) {
    vga_print("ticks: 0x");
    serial_print("ticks: 0x");
    print_hex(g_tick_count);
}

static void cmd_alloc(void) {
    void* p = kalloc(64);
    if (p == NULL) {
        vga_print("alloc failed - heap full");
        serial_print("alloc failed - heap full");
    } else {
        g_last_alloc = p;
        vga_print("allocated 64 bytes at 0x");
        serial_print("allocated 64 bytes at 0x");
        print_hex((u64) p);
    }
}

static void cmd_big_alloc(void) {
    void* p = kalloc(65536);  // bigger than one heap_grow() chunk
    if (p == NULL) {
        vga_print("bigalloc failed - heap full");
        serial_print("bigalloc failed - heap full");
    } else {
        g_last_alloc = p;
        vga_print("allocated 65536 bytes at 0x");
        serial_print("allocated 65536 bytes at 0x");
        print_hex((u64) p);
    }
}

static void cmd_free(void) {
    if (g_last_alloc == NULL) {
        vga_print("nothing to free");
        serial_print("nothing to free");
        return;
    }
    vga_print("freed 0x");
    serial_print("freed 0x");
    print_hex((u64) g_last_alloc);
    kfree(g_last_alloc);
    g_last_alloc = NULL;
}

static void cmd_free_addr(void) {
    u64 addr = parse_hex(&g_line_buffer[5]);  // past "free "
    kfree((void*) addr);
    vga_print("freed 0x");
    serial_print("freed 0x");
    print_hex(addr);
}

static void cmd_mem(void) {
    vga_print("free: 0x");
    serial_print("free: 0x");
    print_hex(heap_free_bytes());
    vga_print(" / 0x");
    serial_print(" / 0x");
    print_hex(g_heap_size);
}

static void cmd_reset(void) {
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
// Closes the GUI Terminal window (proc/apps/terminal/terminal.c
// registers its own window id via syscall 58 the moment it creates it -
// see kernel/gfx/window/window.h's g_terminal_window_id) - a real "exit"
// like typing exit in a real terminal closes it, not a console-shell
// exit (there's nowhere for the console itself to exit to).
static void cmd_exit(void) {
    if (g_terminal_window_id < 0) {
        vga_print("exit: no terminal window open");
        serial_print("exit: no terminal window open");
        return;
    }
    window_close(g_terminal_window_id);
    g_terminal_window_id = -1;
    vga_print("closed terminal window");
    serial_print("closed terminal window");
}

// Real generic Socket object over kernel/net/tcp/tcp.c's real TCP server
// listen/accept (see ring3prog.c trigger 20) - echoes back whatever a
// real external client sends, 3 rounds.
static void cmd_ring3_tcp_server(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x14);
    if (!ok) {
        vga_print("ring3tcpserver failed - channel full");
        serial_print("ring3tcpserver failed - channel full");
        return;
    }
    vga_print("sent ring3 tcp-server trigger - listening on port 9000");
    serial_print("sent ring3 tcp-server trigger - listening on port 9000");
}

// Real lowercase font glyphs + gui_toolkit.h's new Label/Checkbox widgets
// (see ring3prog.c trigger 21).
static void cmd_ring3_widgets(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x15);
    if (!ok) {
        vga_print("ring3widgets failed - channel full");
        serial_print("ring3widgets failed - channel full");
        return;
    }
    vga_print("sent ring3 widgets trigger");
    serial_print("sent ring3 widgets trigger");
}

// Real window focus + keyboard-to-window routing (see ring3prog.c
// trigger 22) - a SEPARATE trigger from ring3widgets, deliberately: this
// one grabs real keyboard focus, which would break ring3widgets' own
// *content commands if they shared a window/trigger.
static void cmd_ring3_focus(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x16);
    if (!ok) {
        vga_print("ring3focus failed - channel full");
        serial_print("ring3focus failed - channel full");
        return;
    }
    vga_print("sent ring3 focus trigger");
    serial_print("sent ring3 focus trigger");
}

// Reads back a pixel inside ring3widgets's checkbox inner fill (see
// ring3prog.c trigger 21). Window w is at screen (400,340), body starts
// at (400,360); checkbox drawn at body-local (10,40) size 16, 2px inset
// -> inner fill screen (412,402)-(423,413). (413,404) - near the top-left
// corner of that range, away from where the cursor tip typically rests
// after a click landed on this box from above - reads 0x00444444
// (unchecked, bg_color) or 0x0000FF00 (checked, check_color), live and
// controllable via QEMU monitor mouse_move/mouse_button, same technique
// cmd_buttoncontent already uses.
static void cmd_checkboxcontent(void) {
    vga_print("checkboxcontent fill=0x");
    serial_print("checkboxcontent fill=0x");
    print_hex((u64) fb_get_pixel(413, 404));
}

// Reads back a pixel inside each of ring3widgets's two radio buttons (see
// ring3prog.c trigger 21). radio0 at body-local (10,70) size 14 -> screen
// (410,430), inner fill (412,432)-(421,441); radio1 at body-local (40,70)
// -> screen (440,430), inner fill (442,432)-(451,441). (413,433)/(443,433)
// - near the top-left corner of each inner range, same "away from the
// cursor's own resting point" reasoning cmd_checkboxcontent already
// documents - read 0x00444444 (unselected, bg_color) or 0x0000FF00
// (selected, dot_color).
static void cmd_radiocontent(void) {
    vga_print("radiocontent r0=0x");
    serial_print("radiocontent r0=0x");
    print_hex((u64) fb_get_pixel(413, 433));
    vga_print(" r1=0x");
    serial_print(" r1=0x");
    print_hex((u64) fb_get_pixel(443, 433));
}

// Reads back two pixels inside ring3widgets's progress bar (body-local
// (10,100) size 150x12 -> screen (410,460)-(560,472)). x=430 sits inside
// the bar's first 30% (410-455) - always fill_color once the demo's
// initial progress_bar_init(..., 30) has run. x=500 sits inside 80%
// (410-530) but past 30% - fill_color only after selecting radio1 sets
// it to 80% (see ring3prog.c trigger 21) - reading both proves the fill
// width genuinely scales with a real progress_bar_set_percent() call,
// not just an on/off flag.
static void cmd_progresscontent(void) {
    vga_print("progresscontent x30pct=0x");
    serial_print("progresscontent x30pct=0x");
    print_hex((u64) fb_get_pixel(430, 466));
    vga_print(" x80pct=0x");
    serial_print(" x80pct=0x");
    print_hex((u64) fb_get_pixel(500, 466));
}

// Reads back the slider's default handle position (body-local (10,120)
// size 150x12, track screen (410,480)-(560,492)). With min=0/max=100/
// initial=50 (see ring3prog.c trigger 21), usable_width=150-6=144,
// handle_offset=50*144/100=72 -> handle screen x=482-488. (485,486) sits
// inside that range - reads 0x0000ff00 (handle_color) by default. After
// a real drag this same point may or may not still show the handle
// (expected - the point is only a fixed baseline check); verify a real
// drag via screendump instead (keyboard dies after the first
// mouse_button of that boot anyway, so a second sendkey-based readback
// isn't possible in the same test run regardless).
static void cmd_slidercontent(void) {
    vga_print("slidercontent handle_at_default=0x");
    serial_print("slidercontent handle_at_default=0x");
    print_hex((u64) fb_get_pixel(485, 486));
}

// Reads back one pixel inside each of ring3widgets's 3 list rows
// (body-local (10,140) width 150 row_height 14 -> screen (410,500)-
// (560,542); row0 y500-514, row1 y514-528, row2 y528-542). x=550 - well
// past any real label's text width (labels here are 5-6 chars, ~36px)
// so this always reads the row's flat background fill, never a glyph's
// foreground pixel. Before any click all three read 0x00101010
// (row_color, none selected); after a real click on a row, that row's
// sample point reads 0x00405070 (selected_color) and the others stay
// row_color.
static void cmd_listcontent(void) {
    vga_print("listcontent row0=0x");
    serial_print("listcontent row0=0x");
    print_hex((u64) fb_get_pixel(550, 505));
    vga_print(" row1=0x");
    serial_print(" row1=0x");
    print_hex((u64) fb_get_pixel(550, 519));
    vga_print(" row2=0x");
    serial_print(" row2=0x");
    print_hex((u64) fb_get_pixel(550, 533));
}

static void cmd_shutdown(void) {
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
static void cmd_reboot(void) {
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
static void cmd_cursor(void) {
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

static void cmd_frames(void) {
    vga_print("free frames: 0x");
    serial_print("free frames: 0x");
    print_hex((u64) g_free_frame_count);
    vga_putc(' ');
    serial_putc(' ');
    vga_print("/ 0x");
    serial_print("/ 0x");
    print_hex((u64) g_total_frames);
}

static void cmd_frame(void) {
    void* f = alloc_frame();
    if (f == NULL) {
        vga_print("out of frames");
        serial_print("out of frames");
    } else {
        g_last_frame = f;
        vga_print("allocated frame at 0x");
        serial_print("allocated frame at 0x");
        print_hex((u64) f);
    }
}

static void cmd_unframe(void) {
    if (g_last_frame == NULL) {
        vga_print("nothing to unframe");
        serial_print("nothing to unframe");
        return;
    }
    vga_print("freed frame 0x");
    serial_print("freed frame 0x");
    print_hex((u64) g_last_frame);
    free_frame(g_last_frame);
    g_last_frame = NULL;
}

static void cmd_map(void) {
    void* frame = alloc_frame();
    if (frame == NULL) {
        vga_print("out of frames");
        serial_print("out of frames");
        return;
    }
    u64 vaddr = 0x40000000;  // 1GB - just past boot.s's static identity map
    bool ok = map_page(vaddr, (u64) frame, 0x02 | PAGE_NX);  // writable, non-executable
    if (!ok) {
        vga_print("map failed");
        serial_print("map failed");
        free_frame(frame);
        return;
    }

    u32* p = (u32*) vaddr;
    p[0] = 0xCAFEBABE;
    u32 read_back = p[0];

    vga_print("mapped 0x");
    serial_print("mapped 0x");
    print_hex(vaddr);
    vga_print(" -> 0x");
    serial_print(" -> 0x");
    print_hex((u64) frame);
    vga_print(", wrote/read 0x");
    serial_print(", wrote/read 0x");
    print_hex((u64) read_back);
}

static void cmd_tasks(void) {
    vga_print("task1: 0x");
    serial_print("task1: 0x");
    print_hex(g_task1_ticks);
    vga_print(" task2: 0x");
    serial_print(" task2: 0x");
    print_hex(g_task2_ticks);
    vga_print(" task3: 0x");
    serial_print(" task3: 0x");
    print_hex(g_task3_ticks);
    vga_print(" task4: 0x");
    serial_print(" task4: 0x");
    print_hex(g_task4_ticks);
    vga_print(" ticks: 0x");
    serial_print(" ticks: 0x");
    print_hex(g_tick_count);
}

static void cmd_procs(void) {
    vga_print("proc_a: 0x");
    serial_print("proc_a: 0x");
    print_hex((u64) g_proc_a_value);
    vga_print(" @phys 0x");
    serial_print(" @phys 0x");
    print_hex(g_proc_a_phys);
    vga_print(" proc_b: 0x");
    serial_print(" proc_b: 0x");
    print_hex((u64) g_proc_b_value);
    vga_print(" @phys 0x");
    serial_print(" @phys 0x");
    print_hex(g_proc_b_phys);
}

static void cmd_chan(void) {
    vga_print("receiver got: 0x");
    serial_print("receiver got: 0x");
    print_hex((u64) g_receiver_got_message);
    vga_print(" value=0x");
    serial_print(" value=0x");
    print_hex(g_receiver_value);
}

static void cmd_send(void) {
    bool ok = channel_send(g_channel_demo, 0xC0FFEE1234);
    if (!ok) {
        vga_print("send failed - channel full");
        serial_print("send failed - channel full");
        return;
    }
    vga_print("sent 0xc0ffee1234");
    serial_print("sent 0xc0ffee1234");
}

static void cmd_clear(void) {
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

static void cmd_ps(void) {
    vga_print("processes: 0x");
    serial_print("processes: 0x");
    print_hex((u64) g_process_count);
    int i = 0;
    while (i < g_process_count) {
        vga_print(" proc");
        serial_print(" proc");
        print_hex((u64) i);
        vga_print(" task=0x");
        serial_print(" task=0x");
        print_hex((u64) g_processes[i].task_index);
        vga_print(" cr3=0x");
        serial_print(" cr3=0x");
        print_hex(g_processes[i].cr3);
        vga_print(" entry=0x");
        serial_print(" entry=0x");
        print_hex(g_tasks[g_processes[i].task_index].ring3_entry_vaddr);
        vga_print(" exited=0x");
        serial_print(" exited=0x");
        print_hex((u64) !g_processes[i].used);
        i = i + 1;
    }
}

static void cmd_objs(void) {
    vga_print("objects: 0x");
    serial_print("objects: 0x");
    print_hex((u64) g_object_count);
    if (g_object_count > 0) {
        vga_print(" obj0 type=0x");
        serial_print(" obj0 type=0x");
        print_hex((u64) g_objects[0].type);
        vga_print(" data_index=0x");
        serial_print(" data_index=0x");
        print_hex((u64) g_objects[0].data_index);
    }
}

static void cmd_netconns(void) {
    vga_print("tcp connections:");
    serial_print("tcp connections:");
    int i = 0;
    while (i < TCP_CONNECTION_SLOTS) {
        if (g_tcp_connections[i].used) {
            vga_print(" conn");
            serial_print(" conn");
            print_hex((u64) i);
            vga_print(" local_port=0x");
            serial_print(" local_port=0x");
            print_hex((u64) g_tcp_connections[i].local_port);
            vga_print(" remote=0x");
            serial_print(" remote=0x");
            print_hex((u64) g_tcp_connections[i].remote_ip[0]);
            vga_print(".0x");
            serial_print(".0x");
            print_hex((u64) g_tcp_connections[i].remote_ip[1]);
            vga_print(".0x");
            serial_print(".0x");
            print_hex((u64) g_tcp_connections[i].remote_ip[2]);
            vga_print(".0x");
            serial_print(".0x");
            print_hex((u64) g_tcp_connections[i].remote_ip[3]);
            vga_print(":0x");
            serial_print(":0x");
            print_hex((u64) g_tcp_connections[i].remote_port);
        }
        i = i + 1;
    }
}

static void cmd_disk(void) {
    u8 buf[512];
    bool ok = ata_read_sector(1, buf);
    if (!ok) {
        vga_print("disk read failed");
        serial_print("disk read failed");
        return;
    }
    char* s = (char*) &buf[0];
    vga_print("sector 1: ");
    serial_print("sector 1: ");
    vga_print(s);
    serial_print(s);
}

static void cmd_disk_write(void) {
    u8 write_buf[512];
    int i = 0;
    while (i < 512) {
        write_buf[i] = (u8) (i & 0xFF);
        i = i + 1;
    }
    bool wrote = ata_write_sector(100, write_buf);
    if (!wrote) {
        vga_print("disk write failed");
        serial_print("disk write failed");
        return;
    }
    u8 read_buf[512];
    bool read_ok = ata_read_sector(100, read_buf);
    if (!read_ok) {
        vga_print("disk write ok, readback failed");
        serial_print("disk write ok, readback failed");
        return;
    }
    bool match = true;
    i = 0;
    while (i < 512) {
        if (write_buf[i] != read_buf[i]) {
            match = false;
        }
        i = i + 1;
    }
    if (match) {
        vga_print("write+readback verified, 512/512 bytes match");
        serial_print("write+readback verified, 512/512 bytes match");
    } else {
        vga_print("MISMATCH - write or read is broken");
        serial_print("MISMATCH - write or read is broken");
    }
}

static void cmd_mkfs(void) {
    bool ok = mkfs();
    if (ok) {
        vga_print("filesystem formatted");
        serial_print("filesystem formatted");
    } else {
        vga_print("mkfs failed");
        serial_print("mkfs failed");
    }
}

// Real VFS-absolute path ("" = the virtual root, "/system", "/system/sub",
// "/devices", "/processes", ...) - same convention
// proc/apps/file_manager.c's own current_path already uses, and the same
// real namespace it browses (this is genuinely the same mount table, not
// a parallel copy). File Manager's own GUI navigation (syscalls 37-39) is
// a separate call path but now resolves through the same VFS backend.
static char g_shell_cwd[128] = "";

// fs_create_dir/fs_delete_file (raw MiniFS, no VFS wrapper exists) and
// the full-screen editor are only ever meaningful inside the one
// writable mount - /devices and /processes reflect live kernel state,
// not something you create/delete/edit files in. Same name/shape as
// proc/apps/file_manager.c's own in_system_mount().
static bool in_system_mount(const char* path) {
    return starts_with(path, "/system");
}

// Strips a leading "/system" (+ one following '/', if present) from a
// real VFS-absolute path, for the raw MiniFS calls that have no VFS
// wrapper - MiniFS's own path resolver (fs/minifs/minifs.c's
// split_path()) has no concept of a mount prefix at all, only bare-
// relative paths. Byte-for-byte the same logic as
// proc/apps/file_manager.c's own strip_system_prefix() (can't share it
// directly - that one runs in ring3, this runs in the kernel).
static void strip_system_prefix(char* out, const char* path) {
    const char* rest = &path[7];  // strlen("/system")
    if (rest[0] == '/') {
        rest = &rest[1];
    }
    int i = 0;
    while (rest[i] != '\0') {
        out[i] = rest[i];
        i = i + 1;
    }
    out[i] = '\0';
}

// Same ~70 command words cmd_help() prints below, minus the <addr>/<dir>/
// <src>/... placeholder tokens (those aren't real command names) - a
// third copy of the same list, but cmd_help() already keeps two literal
// copies (vga_print/serial_print) side by side, so this is consistent
// with, not a departure from, how this codebase already tolerates that
// duplication at this size.
//
// A static initializer can't fill this array directly - each element is
// one global (a string literal) address stored as another global's
// static data, which needs a real ELF64 relocation (R_X86_64_64) this
// kernel's ELF32 build container can't represent (same constraint
// kernel/gfx/cursor_image.h documents for g_cursor_image.pixels). Fixed
// the same way: assign every pointer at runtime instead (real `lea`/`mov`
// instructions, which -fPIC handles fine), lazily on first use.
#define SHELL_COMMAND_COUNT 87
static const char* g_shell_commands[SHELL_COMMAND_COUNT];
static bool g_shell_commands_initialized;

static void shell_commands_init(void) {
    if (g_shell_commands_initialized) {
        return;
    }
    g_shell_commands[0] = "help"; g_shell_commands[1] = "clear"; g_shell_commands[2] = "ticks";
    g_shell_commands[3] = "alloc"; g_shell_commands[4] = "bigalloc"; g_shell_commands[5] = "free";
    g_shell_commands[6] = "mem"; g_shell_commands[7] = "reset"; g_shell_commands[8] = "shutdown";
    g_shell_commands[9] = "reboot"; g_shell_commands[10] = "cursor"; g_shell_commands[11] = "frame";
    g_shell_commands[12] = "unframe"; g_shell_commands[13] = "frames"; g_shell_commands[14] = "map";
    g_shell_commands[15] = "tasks"; g_shell_commands[16] = "procs"; g_shell_commands[17] = "ps";
    g_shell_commands[18] = "objs"; g_shell_commands[19] = "netconns"; g_shell_commands[20] = "chan";
    g_shell_commands[21] = "send"; g_shell_commands[22] = "disk"; g_shell_commands[23] = "diskwrite";
    g_shell_commands[24] = "mkfs"; g_shell_commands[25] = "mkfile"; g_shell_commands[26] = "cat";
    g_shell_commands[27] = "ls"; g_shell_commands[28] = "pwd"; g_shell_commands[29] = "cd";
    g_shell_commands[30] = "mkdir"; g_shell_commands[31] = "cp"; g_shell_commands[32] = "mv";
    g_shell_commands[33] = "touch"; g_shell_commands[34] = "edit"; g_shell_commands[35] = "vfscat";
    g_shell_commands[36] = "vfswrite"; g_shell_commands[37] = "install"; g_shell_commands[38] = "spawn";
    g_shell_commands[39] = "ring3go"; g_shell_commands[40] = "ring3fault"; g_shell_commands[41] = "ring3nx";
    g_shell_commands[42] = "ring3reg"; g_shell_commands[43] = "ring3unreg"; g_shell_commands[44] = "ring3async";
    g_shell_commands[45] = "ring3asyncwrite"; g_shell_commands[46] = "ring3asyncping"; g_shell_commands[47] = "ring3asyncdns";
    g_shell_commands[48] = "ring3asynctcp"; g_shell_commands[49] = "ring3win"; g_shell_commands[50] = "ring3mouse";
    g_shell_commands[51] = "ring3text"; g_shell_commands[52] = "ring3button"; g_shell_commands[53] = "pci";
    g_shell_commands[54] = "nic"; g_shell_commands[55] = "fb"; g_shell_commands[56] = "text";
    g_shell_commands[57] = "mouse"; g_shell_commands[58] = "win"; g_shell_commands[59] = "winlist";
    g_shell_commands[60] = "wincontent"; g_shell_commands[61] = "textcontent"; g_shell_commands[62] = "buttoncontent";
    g_shell_commands[63] = "desktop"; g_shell_commands[64] = "arp"; g_shell_commands[65] = "ping";
    g_shell_commands[66] = "ipconfig"; g_shell_commands[67] = "dns"; g_shell_commands[68] = "tcp";
    g_shell_commands[69] = "echo"; g_shell_commands[70] = "pngtest";
    g_shell_commands[71] = "ring3fileobj"; g_shell_commands[72] = "ring3perms";
    g_shell_commands[73] = "ring3posix";
    g_shell_commands[74] = "ring3pipe"; g_shell_commands[75] = "ring3shm";
    g_shell_commands[76] = "devices"; g_shell_commands[77] = "exit";
    g_shell_commands[78] = "ring3tcpserver";
    g_shell_commands[79] = "service";
    g_shell_commands[80] = "ring3widgets"; g_shell_commands[81] = "checkboxcontent";
    g_shell_commands[82] = "radiocontent"; g_shell_commands[83] = "progresscontent";
    g_shell_commands[84] = "slidercontent"; g_shell_commands[85] = "listcontent";
    g_shell_commands[86] = "ring3focus";
    g_shell_commands_initialized = true;
}

// Erases exactly `count` characters from the end of the in-progress line
// (a bounded version of shell_history_redraw()'s own erase loop - that
// one always clears the whole line, this only clears the word currently
// being completed) then retypes `text`.
static void tab_replace_word(int count, const char* text) {
    while (count > 0) {
        g_line_len = g_line_len - 1;
        g_vga_cursor = g_vga_cursor - 1;
        g_vga[g_vga_cursor].character = ' ';
        g_vga[g_vga_cursor].color = 0x0F;
        serial_putc('\b');
        serial_putc(' ');
        serial_putc('\b');
        term_scrollback_backspace();
        count = count - 1;
    }
    vga_update_cursor(g_vga_cursor);

    int i = 0;
    while (text[i] != '\0' && g_line_len < 127) {
        char c = text[i];
        g_line_buffer[g_line_len] = c;
        g_line_len = g_line_len + 1;
        vga_putc(c);
        serial_putc((u8) c);
        i = i + 1;
    }
}

void shell_tab_complete(void) {
    shell_commands_init();

    // Completion always operates on the LAST word in the line (word_start
    // is computed from g_line_len below, not from wherever the cursor
    // happens to be) - if the cursor was moved mid-line (Left/Right
    // arrow), walk it back out to the end first, mirroring every step, so
    // tab_replace_word()'s own erase/retype (which assumes it's editing
    // right at the end) stays correct instead of silently editing the
    // wrong screen position.
    while (g_line_cursor < g_line_len) {
        g_vga_cursor = g_vga_cursor + 1;
        g_line_cursor = g_line_cursor + 1;
        term_scrollback_cursor_right();
    }

    // g_line_buffer is only null-terminated at g_line_len on Enter (isr.c) -
    // mid-typing it can hold stale trailing bytes from a previous, longer
    // command. Every string op below treats g_line_buffer as a real
    // C-string, so mark the real end here first (safe: isr.c already
    // guarantees g_line_len < 127 on every insert).
    g_line_buffer[g_line_len] = '\0';

    int word_start = 0;
    int i = g_line_len - 1;
    while (i >= 0) {
        if (g_line_buffer[i] == ' ') {
            word_start = i + 1;
            break;
        }
        i = i - 1;
    }
    int word_len = g_line_len - word_start;
    const char* word = &g_line_buffer[word_start];
    bool completing_command = (word_start == 0);

    // Collect matches - command names, or the current directory's own
    // entries for an argument (same vfs_list_entry loop cmd_ls already
    // uses). Names are copied into a local buffer since vfs_list_entry
    // hands back one entry at a time, not a stable pointer.
    char matches[MINIFS_MAX_FILES][20];
    int match_count = 0;

    if (completing_command) {
        int c = 0;
        while (c < SHELL_COMMAND_COUNT && match_count < MINIFS_MAX_FILES) {
            if (starts_with(g_shell_commands[c], word) && strlen_(g_shell_commands[c]) >= word_len) {
                int j = 0;
                while (g_shell_commands[c][j] != '\0' && j < 19) {
                    matches[match_count][j] = g_shell_commands[c][j];
                    j = j + 1;
                }
                matches[match_count][j] = '\0';
                match_count = match_count + 1;
            }
            c = c + 1;
        }
    } else {
        int idx = 0;
        while (idx < MINIFS_MAX_FILES) {
            char name[20];
            u32 size;
            bool is_dir;
            if (vfs_list_entry(g_shell_cwd, idx, name, &size, &is_dir)) {
                if (starts_with(name, word)) {
                    int j = 0;
                    while (name[j] != '\0' && j < 19) {
                        matches[match_count][j] = name[j];
                        j = j + 1;
                    }
                    matches[match_count][j] = '\0';
                    match_count = match_count + 1;
                }
            }
            idx = idx + 1;
        }
    }

    if (match_count == 0) {
        return;
    }

    // Longest common prefix across every match, starting from what's
    // already typed - completes as far as it unambiguously can even with
    // several matches (e.g. "ring3a" among the 5 ring3async* commands).
    int common_len = strlen_(matches[0]);
    int m = 1;
    while (m < match_count) {
        int len = 0;
        while (len < common_len && matches[m][len] == matches[0][len]) {
            len = len + 1;
        }
        common_len = len;
        m = m + 1;
    }

    if (common_len > word_len) {
        char prefix[20];
        int j = 0;
        while (j < common_len) {
            prefix[j] = matches[0][j];
            j = j + 1;
        }
        prefix[j] = '\0';
        tab_replace_word(word_len, prefix);
    }

    if (match_count == 1) {
        // Ready for the next argument (or Enter) - no special-casing a
        // directory match with a trailing '/', a deliberate simplification.
        if (g_line_len < 127) {
            g_line_buffer[g_line_len] = ' ';
            g_line_len = g_line_len + 1;
            vga_putc(' ');
            serial_putc(' ');
        }
        g_line_cursor = g_line_len;
        return;
    }

    // Ambiguous beyond the common prefix - list every candidate, then
    // reprint the prompt and the in-progress line exactly as it was,
    // same as a real shell's own ambiguous-Tab behavior.
    new_line();
    int k = 0;
    while (k < match_count) {
        vga_print(matches[k]);
        serial_print(matches[k]);
        vga_print(" ");
        serial_print(" ");
        k = k + 1;
    }
    new_line();
    print_prompt();
    int p = 0;
    while (p < g_line_len) {
        vga_putc(g_line_buffer[p]);
        serial_putc((u8) g_line_buffer[p]);
        p = p + 1;
    }
    g_line_cursor = g_line_len;
}

static int g_next_file_index;
static char g_last_file_name[128];  // full path, not just the bare name - cmd_cat needs no cwd logic of its own

// Creates a new file each call, inside the current directory: file0.mfs, file1.mfs, ...
static void cmd_mkfile(void) {
    char name_buf[20];
    name_buf[0] = 'f'; name_buf[1] = 'i'; name_buf[2] = 'l'; name_buf[3] = 'e';
    name_buf[4] = (char) ('0' + (u8) (g_next_file_index % 10));
    name_buf[5] = '.'; name_buf[6] = 'm'; name_buf[7] = 'f'; name_buf[8] = 's';
    name_buf[9] = '\0';

    char content_buf[64];
    const char* prefix = "Hello from MiniFS, this is file #";
    int i = 0;
    while (prefix[i] != '\0') {
        content_buf[i] = prefix[i];
        i = i + 1;
    }
    content_buf[i] = (char) ('0' + (u8) (g_next_file_index % 10));
    i = i + 1;
    content_buf[i] = '\0';
    i = i + 1;

    char full_path[128];
    join_path(full_path, g_shell_cwd, name_buf);

    bool ok = vfs_write(full_path, (u8*) &content_buf[0], (u32) i);
    if (!ok) {
        vga_print("mkfile failed");
        serial_print("mkfile failed");
        return;
    }
    copy_name(&g_last_file_name[0], full_path);
    g_next_file_index = g_next_file_index + 1;
    vga_print("created ");
    serial_print("created ");
    vga_print(name_buf);
    serial_print(name_buf);
}

static void cmd_cat(void) {
    if (g_last_file_name[0] == '\0') {
        vga_print("no file yet - run mkfile first");
        serial_print("no file yet - run mkfile first");
        return;
    }
    u8 buf[65];
    int n = vfs_read(&g_last_file_name[0], buf, 64);
    if (n < 0) {
        vga_print("cat failed");
        serial_print("cat failed");
        return;
    }
    buf[n] = 0;
    char* s = (char*) &buf[0];
    vga_print(s);
    serial_print(s);
}

static void cmd_ls(void) {
    // file_count is a whole-MiniFS-volume stat (fs_superblock_info) - it
    // only means something while actually browsing /system; the virtual
    // root (the mount table itself) and /devices/processes (live kernel
    // state, not a MiniFS volume) have no such concept.
    if (in_system_mount(g_shell_cwd)) {
        u32 file_count;
        if (!fs_superblock_info(&file_count)) {
            vga_print("ls failed - disk read error");
            serial_print("ls failed - disk read error");
            return;
        }
        vga_print("file_count: 0x");
        serial_print("file_count: 0x");
        print_hex((u64) file_count);
        vga_print("  ");
        serial_print("  ");
    }

    int i = 0;
    int shown = 0;
    while (i < MINIFS_MAX_FILES) {
        char name[20];
        u32 size;
        bool is_dir;
        if (vfs_list_entry(g_shell_cwd, i, name, &size, &is_dir)) {
            vga_print(name);
            serial_print(name);
            if (is_dir) {
                vga_print("/");
                serial_print("/");
            }
            vga_print(" 0x");
            serial_print(" 0x");
            print_hex((u64) size);
            vga_print("  ");
            serial_print("  ");
            shown = shown + 1;
        }
        i = i + 1;
    }
    if (shown == 0) {
        vga_print("(empty)");
        serial_print("(empty)");
    }
}

static void cmd_pwd(void) {
    if (g_shell_cwd[0] == '\0') {
        vga_print("/");
        serial_print("/");
        return;
    }
    vga_print(g_shell_cwd);
    serial_print(g_shell_cwd);
}

// `cd` (no arg) or `cd /` -> root. `cd ..` -> strip the last component
// (same logic as proc/apps/file_manager.c's navigate_up, already correct
// for an absolute path: truncating "/system" at its last '/' correctly
// yields "", the virtual root). Otherwise resolve the arg against the
// current dir and only commit if a real vfs_list_entry() scan finds a
// same-named directory entry - this one check transparently covers
// entering a top-level mount from root, a real MiniFS subdirectory, and
// correctly refusing to "enter" a devfs/procfs pseudo-file (never
// is_dir), with no per-backend special-casing here. A failed cd leaves
// g_shell_cwd untouched, never half-updated.
static void cmd_cd_root(void) {
    g_shell_cwd[0] = '\0';
}

static void cmd_cd(void) {
    char* arg = &g_line_buffer[3];  // past "cd "
    if (streq(arg, "/")) {
        cmd_cd_root();
        return;
    }
    if (streq(arg, "..")) {
        int len = strlen_(g_shell_cwd);
        int i = len - 1;
        while (i >= 0 && g_shell_cwd[i] != '/') {
            i = i - 1;
        }
        if (i < 0) {
            g_shell_cwd[0] = '\0';
        } else {
            g_shell_cwd[i] = '\0';
        }
        return;
    }

    bool found = false;
    int idx = 0;
    while (idx < MINIFS_MAX_FILES) {
        char name[20];
        u32 size;
        bool is_dir;
        if (vfs_list_entry(g_shell_cwd, idx, name, &size, &is_dir)) {
            if (is_dir && streq(name, arg)) {
                found = true;
                break;
            }
        }
        idx = idx + 1;
    }
    if (!found) {
        vga_print("cd: no such directory");
        serial_print("cd: no such directory");
        return;
    }

    char new_path[128];
    join_path(new_path, g_shell_cwd, arg);
    int i = 0;
    while (new_path[i] != '\0' && i < 127) {
        g_shell_cwd[i] = new_path[i];
        i = i + 1;
    }
    g_shell_cwd[i] = '\0';
}

static void cmd_mkdir(void) {
    char* arg = &g_line_buffer[6];  // past "mkdir "
    char new_path[128];
    join_path(new_path, g_shell_cwd, arg);
    if (!in_system_mount(new_path)) {
        vga_print("mkdir: not writable here");
        serial_print("mkdir: not writable here");
        return;
    }
    char stripped[128];
    strip_system_prefix(stripped, new_path);
    bool ok = fs_create_dir(stripped);
    if (!ok) {
        vga_print("mkdir failed");
        serial_print("mkdir failed");
        return;
    }
    vga_print("created ");
    serial_print("created ");
    vga_print(arg);
    serial_print(arg);
}

// Splits "<first> <second>" (whatever follows a command's own prefix,
// e.g. args = &g_line_buffer[3] past "cp ") into two path fragments -
// shared by cp/mv, the only two commands here that take two arguments.
// No tokenizer exists in this codebase - same manual space-scan style as
// every other multi-word-free command. Returns false (usage error
// already printed) if there's no second argument.
static bool split_two_args(char* args, char* first_out, char** second_out) {
    int space = 0;
    while (args[space] != '\0' && args[space] != ' ') {
        space = space + 1;
    }
    if (args[space] == '\0') {
        vga_print("usage: <cmd> <src> <dst>");
        serial_print("usage: <cmd> <src> <dst>");
        return false;
    }
    int i = 0;
    while (i < space) {
        first_out[i] = args[i];
        i = i + 1;
    }
    first_out[i] = '\0';
    *second_out = &args[space + 1];
    return true;
}

// cp <src> <dst>, both resolved against the current directory. Unlike
// mkfile's deliberate create-only demo semantics, a real `cp` is
// expected to overwrite an existing destination - MiniFS's
// fs_write_file refuses to write over an existing file (same limitation
// this session's Settings fix already hit for /system/settings.cfg), so
// delete-then-write is the only way to get real overwrite behavior.
static void cmd_cp(void) {
    char src_name[64];
    char* dst_name;
    if (!split_two_args(&g_line_buffer[3], src_name, &dst_name)) {  // past "cp "
        return;
    }

    char src_path[128];
    char dst_path[128];
    join_path(src_path, g_shell_cwd, src_name);
    join_path(dst_path, g_shell_cwd, dst_name);

    u8 buf[4096];
    int n = vfs_read(src_path, buf, sizeof(buf));
    if (n == -2) {
        vga_print("cp: source too large");
        serial_print("cp: source too large");
        return;
    }
    if (n < 0) {
        vga_print("cp: source not found");
        serial_print("cp: source not found");
        return;
    }
    if (!in_system_mount(dst_path)) {
        vga_print("cp: destination not writable");
        serial_print("cp: destination not writable");
        return;
    }
    char stripped_dst[128];
    strip_system_prefix(stripped_dst, dst_path);
    fs_delete_file(stripped_dst);  // overwrite semantics - failure here just means dst didn't exist yet, expected
    bool ok = vfs_write(dst_path, buf, (u32) n);
    if (!ok) {
        vga_print("cp failed");
        serial_print("cp failed");
        return;
    }
    vga_print("copied");
    serial_print("copied");
}

// mv <src> <dst> - MiniFS has no real in-place rename, so this is
// genuinely copy-then-delete-original, same honesty as every other
// documented limitation in this codebase, not hidden behind a
// misleadingly "atomic-sounding" name.
static void cmd_mv(void) {
    char src_name[64];
    char* dst_name;
    if (!split_two_args(&g_line_buffer[3], src_name, &dst_name)) {  // past "mv "
        return;
    }

    char src_path[128];
    char dst_path[128];
    join_path(src_path, g_shell_cwd, src_name);
    join_path(dst_path, g_shell_cwd, dst_name);

    u8 buf[4096];
    int n = vfs_read(src_path, buf, sizeof(buf));
    if (n == -2) {
        vga_print("mv: source too large");
        serial_print("mv: source too large");
        return;
    }
    if (n < 0) {
        vga_print("mv: source not found");
        serial_print("mv: source not found");
        return;
    }
    if (!in_system_mount(dst_path)) {
        vga_print("mv: destination not writable");
        serial_print("mv: destination not writable");
        return;
    }
    char stripped_dst[128];
    strip_system_prefix(stripped_dst, dst_path);
    fs_delete_file(stripped_dst);  // overwrite semantics, same as cp
    bool ok = vfs_write(dst_path, buf, (u32) n);
    if (!ok) {
        vga_print("mv failed");
        serial_print("mv failed");
        return;
    }
    if (in_system_mount(src_path)) {
        char stripped_src[128];
        strip_system_prefix(stripped_src, src_path);
        fs_delete_file(stripped_src);
    }
    vga_print("moved");
    serial_print("moved");
}

// touch <name> - creates an empty file. Unlike mkfile's deliberate
// create-only demo semantics, real touch never fails just because the
// file already exists (it would normally just update a timestamp there -
// no timestamp concept exists in this kernel at all, so an existing file
// is simply left alone and still reported as success).
static void cmd_touch(void) {
    char* arg = &g_line_buffer[6];  // past "touch "
    char path[128];
    join_path(path, g_shell_cwd, arg);
    if (!in_system_mount(path)) {
        vga_print("touch: not writable here");
        serial_print("touch: not writable here");
        return;
    }
    u8 empty = 0;
    vfs_write(path, &empty, 0);  // ignored, same as before: never fails just because the file already exists
    vga_print("touched ");
    serial_print("touched ");
    vga_print(arg);
    serial_print(arg);
}

// edit <name> - full-screen console text editor, see shell/editor.c. Takes
// over the whole display until Esc; nothing else runs on this task while
// g_editor_active is true (kmain.c's main loop skips its own prompt
// reprint, isr.c routes every keystroke to editor_handle_scancode()).
// editor.c itself stays completely VFS-unaware (raw MiniFS calls only) -
// gate here and pass it the stripped bare-relative path.
static void cmd_edit(void) {
    char* arg = &g_line_buffer[5];  // past "edit "
    char path[128];
    join_path(path, g_shell_cwd, arg);
    if (!in_system_mount(path)) {
        vga_print("edit: not writable here");
        serial_print("edit: not writable here");
        return;
    }
    char stripped[128];
    strip_system_prefix(stripped, path);
    editor_start(stripped);
}

// cat <name> - reads an explicit path relative to the current directory,
// unlike bare `cat` (still kept, unchanged) which only ever replays
// whatever `mkfile` last created.
static void cmd_cat_path(void) {
    char* arg = &g_line_buffer[4];  // past "cat "
    char path[128];
    join_path(path, g_shell_cwd, arg);
    u8 buf[65];
    int n = vfs_read(path, buf, 64);
    if (n == -2) {
        vga_print("cat: file too large to display");
        serial_print("cat: file too large to display");
        return;
    }
    if (n < 0) {
        vga_print("cat: file not found");
        serial_print("cat: file not found");
        return;
    }
    buf[n] = 0;
    char* s = (char*) &buf[0];
    vga_print(s);
    serial_print(s);
}

static void cmd_vfs_cat(void) {
    char* path = &g_line_buffer[7];  // past "vfscat "
    u8 buf[256];
    int n = vfs_read(path, buf, 256);
    if (n == -2) {
        vga_print("vfscat: file too large to display");
        serial_print("vfscat: file too large to display");
        return;
    }
    if (n < 0) {
        vga_print("vfscat: not found");
        serial_print("vfscat: not found");
        return;
    }
    buf[n] = 0;
    char* s = (char*) &buf[0];
    vga_print(s);
    serial_print(s);
}

static void cmd_vfs_write(void) {
    const char* content = "This file was written through the VFS layer, not MiniFS directly.";
    int len = strlen_(content) + 1;  // include the null terminator, same as mkfile's content
    bool ok = vfs_write("/system/vfsdemo.mfs", (u8*) content, (u32) len);
    if (ok) {
        vga_print("wrote /system/vfsdemo.mfs via VFS");
        serial_print("wrote /system/vfsdemo.mfs via VFS");
    } else {
        vga_print("vfswrite failed");
        serial_print("vfswrite failed");
    }
}

static void cmd_install(void) {
    u32 len = (u32) ((u64) &g_test_prog_end - (u64) &g_test_prog_start);
    bool ok = vfs_write("/system/testprog.bin", &g_test_prog_start, len);
    if (!ok) {
        vga_print("install failed");
        serial_print("install failed");
        return;
    }
    vga_print("installed /system/testprog.bin, 0x");
    serial_print("installed /system/testprog.bin, 0x");
    print_hex((u64) len);
    vga_print(" bytes");
    serial_print(" bytes");
}

static void cmd_spawn(void) {
    int idx = spawn_process_from_path("/system/testprog.bin", 0x80000000, 0x80020000);
    if (idx < 0) {
        vga_print("spawn failed");
        serial_print("spawn failed");
        return;
    }
    vga_print("spawned process 0x");
    serial_print("spawned process 0x");
    print_hex((u64) idx);
}

// Wakes the boot-time ring3 process's blocked Channel.receive() -
// run `install` first so the file it spawns exists on disk.
static void cmd_ring3_go(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x1);
    if (!ok) {
        vga_print("ring3go failed - channel full");
        serial_print("ring3go failed - channel full");
        return;
    }
    vga_print("sent ring3 spawn trigger");
    serial_print("sent ring3 spawn trigger");
}

// Triggers a deliberate forbidden write - KERNEL-HALTING, run standalone.
static void cmd_ring3_fault(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x2);
    if (!ok) {
        vga_print("ring3fault failed - channel full");
        serial_print("ring3fault failed - channel full");
        return;
    }
    vga_print("sent ring3 forbidden-write trigger - expect a page fault");
    serial_print("sent ring3 forbidden-write trigger - expect a page fault");
}

// Triggers a stack-execution attempt - KERNEL-HALTING, run standalone.
static void cmd_ring3_nx(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x3);
    if (!ok) {
        vga_print("ring3nx failed - channel full");
        serial_print("ring3nx failed - channel full");
        return;
    }
    vga_print("sent ring3 stack-execution trigger - expect a page fault");
    serial_print("sent ring3 stack-execution trigger - expect a page fault");
}

// Registers testprog.bin at runtime and spawns it by index - run `install` first.
static void cmd_ring3_register(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x4);
    if (!ok) {
        vga_print("ring3reg failed - channel full");
        serial_print("ring3reg failed - channel full");
        return;
    }
    vga_print("sent ring3 register-service trigger");
    serial_print("sent ring3 register-service trigger");
}

// Registers, unregisters, then re-registers to confirm slot reuse - run `install` first.
static void cmd_ring3_unregister(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x5);
    if (!ok) {
        vga_print("ring3unreg failed - channel full");
        serial_print("ring3unreg failed - channel full");
        return;
    }
    vga_print("sent ring3 unregister-service trigger");
    serial_print("sent ring3 unregister-service trigger");
}

// Issues an async read, does work, then collects the result.
static void cmd_ring3_async(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x6);
    if (!ok) {
        vga_print("ring3async failed - channel full");
        serial_print("ring3async failed - channel full");
        return;
    }
    vga_print("sent ring3 async-read trigger");
    serial_print("sent ring3 async-read trigger");
}

// Issues an async write, then verifies it via a sync read.
static void cmd_ring3_async_write(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x7);
    if (!ok) {
        vga_print("ring3asyncwrite failed - channel full");
        serial_print("ring3asyncwrite failed - channel full");
        return;
    }
    vga_print("sent ring3 async-write trigger");
    serial_print("sent ring3 async-write trigger");
}

// Issues an async ICMP ping to the gateway.
static void cmd_ring3_async_ping(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x8);
    if (!ok) {
        vga_print("ring3asyncping failed - channel full");
        serial_print("ring3asyncping failed - channel full");
        return;
    }
    vga_print("sent ring3 async-ping trigger");
    serial_print("sent ring3 async-ping trigger");
}

// Issues an async DNS resolve for example.com.
static void cmd_ring3_async_dns(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x9);
    if (!ok) {
        vga_print("ring3asyncdns failed - channel full");
        serial_print("ring3asyncdns failed - channel full");
        return;
    }
    vga_print("sent ring3 async-dns trigger");
    serial_print("sent ring3 async-dns trigger");
}

// Chains an async DNS resolve into an async TCP fetch of example.com.
static void cmd_ring3_async_tcp(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0xA);
    if (!ok) {
        vga_print("ring3asynctcp failed - channel full");
        serial_print("ring3asynctcp failed - channel full");
        return;
    }
    vga_print("sent ring3 async-tcp trigger");
    serial_print("sent ring3 async-tcp trigger");
}

// Creates/raises/moves/closes real windows via the window syscalls.
static void cmd_ring3_window(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0xB);
    if (!ok) {
        vga_print("ring3win failed - channel full");
        serial_print("ring3win failed - channel full");
        return;
    }
    vga_print("sent ring3 window trigger");
    serial_print("sent ring3 window trigger");
}

// Polls real mouse state 3 times with real work in between.
static void cmd_ring3_mouse(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0xC);
    if (!ok) {
        vga_print("ring3mouse failed - channel full");
        serial_print("ring3mouse failed - channel full");
        return;
    }
    vga_print("sent ring3 mouse trigger");
    serial_print("sent ring3 mouse trigger");
}

// Draws real text into a window via the window_draw_text syscall.
static void cmd_ring3_text(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0xD);
    if (!ok) {
        vga_print("ring3text failed - channel full");
        serial_print("ring3text failed - channel full");
        return;
    }
    vga_print("sent ring3 text trigger");
    serial_print("sent ring3 text trigger");
}

// Polls a real Button widget (gui_toolkit.h) 6 times against live mouse+
// window state, redrawing pressed/normal each time.
static void cmd_ring3_button(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0xE);
    if (!ok) {
        vga_print("ring3button failed - channel full");
        serial_print("ring3button failed - channel full");
        return;
    }
    vga_print("sent ring3 button trigger");
    serial_print("sent ring3 button trigger");
}

// Exercises real persistent File kernel objects (proc/ipc/file/file.h,
// syscalls 44-48, see ring3prog.c trigger 15) - incremental cursor reads,
// enforced READ/WRITE handle rights, buffered-write-commit-on-close.
static void cmd_ring3_file_object(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0xF);
    if (!ok) {
        vga_print("ring3fileobj failed - channel full");
        serial_print("ring3fileobj failed - channel full");
        return;
    }
    vga_print("sent ring3 file-object trigger");
    serial_print("sent ring3 file-object trigger");
}

// Exercises real UID-based file ownership + permission enforcement
// (kernel/fs/minifs/minifs.h's MODE_OWNER_ONLY_READ, see ring3prog.c
// trigger 16): owner read succeeds, a non-owner non-root uid is refused,
// root bypasses.
static void cmd_ring3_perms(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x10);
    if (!ok) {
        vga_print("ring3perms failed - channel full");
        serial_print("ring3perms failed - channel full");
        return;
    }
    vga_print("sent ring3 permissions trigger");
    serial_print("sent ring3 permissions trigger");
}

// Exercises the real POSIX shim (proc/posix/posix.h) - open/read/write/
// close/lseek over the same File-object syscalls as ring3fileobj, see
// ring3prog.c trigger 17.
static void cmd_ring3_posix(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x11);
    if (!ok) {
        vga_print("ring3posix failed - channel full");
        serial_print("ring3posix failed - channel full");
        return;
    }
    vga_print("sent ring3 posix trigger");
    serial_print("sent ring3 posix trigger");
}

// Writes two separate short strings directly into the well-known boot-
// time pipe (kernel-side pipe_write - no syscall needed, shell.c is
// ring0) before sending the trigger, so ring3prog.c's trigger 18 has to
// really reassemble multiple writes out of one ring buffer, not just
// echo back one clean write.
static void cmd_ring3_pipe(void) {
    pipe_write(g_ring3_pipe_demo, (const u8*) "hello ", 6);
    pipe_write(g_ring3_pipe_demo, (const u8*) "from the pipe!", 14);
    bool ok = channel_send(g_ring3_channel_demo, 0x12);
    if (!ok) {
        vga_print("ring3pipe failed - channel full");
        serial_print("ring3pipe failed - channel full");
        return;
    }
    vga_print("wrote to pipe, sent ring3 pipe trigger");
    serial_print("wrote to pipe, sent ring3 pipe trigger");
}

// Real frame-backed SharedMemory (see ring3prog.c trigger 19).
static void cmd_ring3_shm(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x13);
    if (!ok) {
        vga_print("ring3shm failed - channel full");
        serial_print("ring3shm failed - channel full");
        return;
    }
    vga_print("sent ring3 shared-memory trigger");
    serial_print("sent ring3 shared-memory trigger");
}

static void print_mac(u8* mac) {
    int i = 0;
    while (i < 6) {
        print_hex((u64) mac[i]);
        if (i < 5) {
            vga_print(":");
            serial_print(":");
        }
        i = i + 1;
    }
}

// Dotted-decimal, not hex - a real IP address, unlike a MAC, is expected
// in decimal by every human and every other tool that will ever look at
// this output (ping/ipconfig/traceroute/etc all agree on this).
static void print_ip(u8* ip) {
    int i = 0;
    while (i < 4) {
        print_decimal((u64) ip[i]);
        if (i < 3) {
            vga_print(".");
            serial_print(".");
        }
        i = i + 1;
    }
}

static void cmd_pci(void) {
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
static void cmd_devices(void) {
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

// service <start|stop|restart|status> <name> - real generic Service
// Manager surface (kernel/services/service_manager.h). "stop" means "don't
// respawn the next time it exits" (see that header's own comment) - this
// kernel has no way to forcibly kill another process.
static void cmd_service(void) {
    char subcmd[32];
    char* name;
    if (!split_two_args(&g_line_buffer[8], subcmd, &name)) {  // past "service "
        return;
    }
    if (streq(subcmd, "start")) {
        bool ok = service_start(name);
        vga_print(ok ? "service started" : "service start failed (unknown name?)");
        serial_print(ok ? "service started" : "service start failed (unknown name?)");
    } else if (streq(subcmd, "stop")) {
        bool ok = service_stop(name);
        vga_print(ok ? "service stopped (will not auto-restart)" : "service stop failed (unknown name?)");
        serial_print(ok ? "service stopped (will not auto-restart)" : "service stop failed (unknown name?)");
    } else if (streq(subcmd, "restart")) {
        bool ok = service_restart(name);
        vga_print(ok ? "service restarted" : "service restart failed (unknown name?)");
        serial_print(ok ? "service restarted" : "service restart failed (unknown name?)");
    } else if (streq(subcmd, "status")) {
        bool running;
        u32 restart_count;
        int process_index;
        if (!service_get_status(name, &running, &restart_count, &process_index)) {
            vga_print("service status failed (unknown name?)");
            serial_print("service status failed (unknown name?)");
            return;
        }
        vga_print("running=0x");
        serial_print("running=0x");
        print_hex((u64) running);
        vga_print(" restart_count=0x");
        serial_print(" restart_count=0x");
        print_hex((u64) restart_count);
        vga_print(" process_index=0x");
        serial_print(" process_index=0x");
        print_hex((u64) (process_index < 0 ? 0xFFFFFFFF : (u32) process_index));
    } else {
        vga_print("usage: service <start|stop|restart|status> <name>");
        serial_print("usage: service <start|stop|restart|status> <name>");
    }
}

static void cmd_nic(void) {
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

// Sets an 800x600x32 linear framebuffer, draws a background fill plus a
// contrasting rect, then reads pixels back (not just the values we sent) to
// prove real hardware round trips, including exact rect-boundary precision.
static void cmd_fb(void) {
    bool ok = vbe_init(800, 600);
    if (!ok) {
        vga_print("framebuffer init failed - no Bochs VBE VGA device found");
        serial_print("framebuffer init failed - no Bochs VBE VGA device found");
        return;
    }

    vga_print("fb lfb_phys=0x");
    serial_print("fb lfb_phys=0x");
    print_hex((u64) vbe_lfb_phys());
    vga_print(" vaddr=0x");
    serial_print(" vaddr=0x");
    print_hex(g_fb_vaddr);
    vga_print(" xres=0x");
    serial_print(" xres=0x");
    print_hex((u64) vbe_read_reg(1));
    vga_print(" yres=0x");
    serial_print(" yres=0x");
    print_hex((u64) vbe_read_reg(2));
    vga_print(" bpp=0x");
    serial_print(" bpp=0x");
    print_hex((u64) vbe_read_reg(3));
    vga_print(" pitch=0x");
    serial_print(" pitch=0x");
    print_hex((u64) g_fb_pitch);

    fb_fill_rect(0, 0, 800, 600, 0x00001133);
    fb_fill_rect(100, 100, 200, 150, 0x00FF0000);

    vga_print(" bg=0x");
    serial_print(" bg=0x");
    print_hex((u64) fb_get_pixel(0, 0));
    vga_print(" just_outside=0x");
    serial_print(" just_outside=0x");
    print_hex((u64) fb_get_pixel(99, 100));
    vga_print(" rect_topleft=0x");
    serial_print(" rect_topleft=0x");
    print_hex((u64) fb_get_pixel(100, 100));
    vga_print(" rect_bottomright=0x");
    serial_print(" rect_bottomright=0x");
    print_hex((u64) fb_get_pixel(299, 249));
    vga_print(" past_rect=0x");
    serial_print(" past_rect=0x");
    print_hex((u64) fb_get_pixel(300, 250));
}

// Draws "HI" straight onto the raw framebuffer with fb_draw_string, then
// reads back specific hand-computed pixels: a foreground stroke in each of
// the two glyphs, an "off" pixel inside a glyph's own cell, the untouched
// inter-character gap column, and a pixel well past both characters - same
// discipline as cmd_fb's rect read-backs.
static void cmd_text(void) {
    bool ok = vbe_init(800, 600);
    if (!ok) {
        vga_print("framebuffer init failed - no Bochs VBE VGA device found");
        serial_print("framebuffer init failed - no Bochs VBE VGA device found");
        return;
    }

    fb_fill_rect(0, 0, 800, 600, 0x00001133);
    fb_draw_string(100, 100, "HI", 0x00FFFFFF, 0x00001133);

    // 'H' bitmap row0=10001 (col0 on), row1=10001 (col1 off), row3=11111
    // (col2 on, the crossbar). 'I' starts at x+6, row0=11111 (col2 on).
    vga_print("text h_stroke=0x");
    serial_print("text h_stroke=0x");
    print_hex((u64) fb_get_pixel(100, 100));
    vga_print(" h_gap=0x");
    serial_print(" h_gap=0x");
    print_hex((u64) fb_get_pixel(101, 101));
    vga_print(" h_crossbar=0x");
    serial_print(" h_crossbar=0x");
    print_hex((u64) fb_get_pixel(102, 103));
    vga_print(" between_chars=0x");
    serial_print(" between_chars=0x");
    print_hex((u64) fb_get_pixel(105, 100));
    vga_print(" i_stroke=0x");
    serial_print(" i_stroke=0x");
    print_hex((u64) fb_get_pixel(108, 100));
    vga_print(" past_text=0x");
    serial_print(" past_text=0x");
    print_hex((u64) fb_get_pixel(200, 100));
}

// Enables the PS/2 mouse and reports its currently tracked state. Safe to
// re-run - init doesn't reset position/buttons/packet count, so running
// this once, injecting real input, then running it again proves a genuine
// hardware round trip rather than just "the driver didn't crash".
static void cmd_mouse(void) {
    mouse_init();
    vga_print("mouse x=0x");
    serial_print("mouse x=0x");
    print_hex((u64) g_mouse_x);
    vga_print(" y=0x");
    serial_print(" y=0x");
    print_hex((u64) g_mouse_y);
    vga_print(" buttons=0x");
    serial_print(" buttons=0x");
    print_hex((u64) g_mouse_buttons);
    vga_print(" packets=0x");
    serial_print(" packets=0x");
    print_hex((u64) g_mouse_packet_count);
    vga_print(" rawbytes=0x");
    serial_print(" rawbytes=0x");
    print_hex((u64) g_mouse_raw_byte_count);
}

// A real window table + Z-order compositor demo: 2 overlapping windows plus
// a 3rd standalone one, then a raise, a move, and a close - each step
// checked by reading the actual composited pixels back, not just trusting
// the calls succeeded.
static void cmd_win(void) {
    bool ok = vbe_init(800, 600);
    if (!ok) {
        vga_print("window server init failed - no framebuffer");
        serial_print("window server init failed - no framebuffer");
        return;
    }

    int a = window_create(50, 50, 200, 150, 0x00FF0000, 0x00800000);
    int b = window_create(150, 120, 200, 150, 0x0000FF00, 0x00008000);
    int c = window_create(500, 400, 150, 100, 0x000000FF, 0x00000080);
    compositor_redraw();

    vga_print("win a=0x");
    serial_print("win a=0x");
    print_hex((u64) a);
    vga_print(" b=0x");
    serial_print(" b=0x");
    print_hex((u64) b);
    vga_print(" c=0x");
    serial_print(" c=0x");
    print_hex((u64) c);

    vga_print(" overlap_b_on_top=0x");
    serial_print(" overlap_b_on_top=0x");
    print_hex((u64) fb_get_pixel(200, 150));
    vga_print(" a_only=0x");
    serial_print(" a_only=0x");
    print_hex((u64) fb_get_pixel(60, 180));
    vga_print(" b_only=0x");
    serial_print(" b_only=0x");
    print_hex((u64) fb_get_pixel(300, 150));
    vga_print(" c_body=0x");
    serial_print(" c_body=0x");
    print_hex((u64) fb_get_pixel(550, 450));
    vga_print(" bg=0x");
    serial_print(" bg=0x");
    print_hex((u64) fb_get_pixel(400, 300));

    window_raise(a);
    compositor_redraw();
    vga_print(" overlap_a_raised=0x");
    serial_print(" overlap_a_raised=0x");
    print_hex((u64) fb_get_pixel(200, 150));

    window_move(c, 550, 420);
    compositor_redraw();
    vga_print(" c_old_after_move=0x");
    serial_print(" c_old_after_move=0x");
    print_hex((u64) fb_get_pixel(510, 450));
    vga_print(" c_new_after_move=0x");
    serial_print(" c_new_after_move=0x");
    print_hex((u64) fb_get_pixel(670, 470));

    window_fill_content_rect(c, 0, 0, 150, 80, 0x00333333);
    window_fill_content_rect(c, 20, 20, 50, 50, 0x00FFFF00);
    compositor_redraw();
    vga_print(" c_content_base=0x");
    serial_print(" c_content_base=0x");
    print_hex((u64) fb_get_pixel(650, 450));
    vga_print(" c_content_accent=0x");
    serial_print(" c_content_accent=0x");
    print_hex((u64) fb_get_pixel(590, 480));

    window_close(b);
    compositor_redraw();
    vga_print(" b_gone=0x");
    serial_print(" b_gone=0x");
    print_hex((u64) fb_get_pixel(300, 150));
    vga_print(" a_survives=0x");
    serial_print(" a_survives=0x");
    print_hex((u64) fb_get_pixel(200, 150));
    vga_print(" windows_left=0x");
    serial_print(" windows_left=0x");
    print_hex((u64) g_window_zorder_count);
}

// Dumps the real window table's current z-order (bottom to top) - id,
// position, size. Same introspection discipline as netconns/objs.
static void cmd_winlist(void) {
    vga_print("windows: 0x");
    serial_print("windows: 0x");
    print_hex((u64) g_window_zorder_count);
    int i = 0;
    while (i < g_window_zorder_count) {
        window* w = &g_windows[g_window_zorder[i]];
        vga_print(" id=0x");
        serial_print(" id=0x");
        print_hex((u64) g_window_zorder[i]);
        vga_print(" x=0x");
        serial_print(" x=0x");
        print_hex((u64) w->x);
        vga_print(" y=0x");
        serial_print(" y=0x");
        print_hex((u64) w->y);
        vga_print(" w=0x");
        serial_print(" w=0x");
        print_hex((u64) w->width);
        vga_print(" h=0x");
        serial_print(" h=0x");
        print_hex((u64) w->height);
        vga_print(" content=0x");
        serial_print(" content=0x");
        print_hex((u64) w->has_content);
        i = i + 1;
    }
}

// Reads back the two pixels ring3win's window d (see ring3prog.c trigger 11)
// should have drawn via window_fill_rect - run this after ring3win.
static void cmd_wincontent(void) {
    vga_print("wincontent base=0x");
    serial_print("wincontent base=0x");
    print_hex((u64) fb_get_pixel(150, 420));
    vga_print(" accent=0x");
    serial_print(" accent=0x");
    print_hex((u64) fb_get_pixel(90, 360));
}

// Reads back pixels ring3text's window e (see ring3prog.c trigger 13) should
// have drawn via window_draw_text - run this after ring3text. Window e is at
// screen (300,300), titlebar 20px, so its body starts at (300,320); text was
// drawn at body-local (10,10) -> screen (310,330). Only checking pixels the
// glyphs explicitly set 'on' (not gap/background pixels, whose value would
// depend on whatever this window slot's content buffer held before - same
// restraint cmd_wincontent already applies to window d).
static void cmd_textcontent(void) {
    // 'O' row0=01110 (offset1 on), row1=10001 (offset0 on, left stroke).
    vga_print("textcontent o_top=0x");
    serial_print("textcontent o_top=0x");
    print_hex((u64) fb_get_pixel(311, 330));
    vga_print(" o_left_stroke=0x");
    serial_print(" o_left_stroke=0x");
    print_hex((u64) fb_get_pixel(310, 331));
    // 'S' starts at body-local x=16 (screen x=300+16=316), row0=01111 (offset1 on).
    vga_print(" s_top=0x");
    serial_print(" s_top=0x");
    print_hex((u64) fb_get_pixel(317, 330));
}

// Reads back a pixel inside ring3button's button fill (see ring3prog.c
// trigger 14) - run this after/while ring3button is polling. Window f is
// at screen (400,200), body starts at (400,220); button drawn at
// body-local (20,30) size 100x30 -> screen (420,250)..(519,279). Pixel
// picked well past the 2-char "OK" label so it reads the flat fill color:
// 0x00888888 (normal) or 0x0000FF00 (pressed, cursor over it + left down),
// live and controllable via QEMU monitor mouse_move/mouse_button.
static void cmd_buttoncontent(void) {
    vga_print("buttoncontent fill=0x");
    serial_print("buttoncontent fill=0x");
    print_hex((u64) fb_get_pixel(470, 260));
}

// Reads back pixels from the desktop shell (proc/apps/desktop_shell.c),
// auto-spawned at boot - no trigger needed, unlike ring3button/ring3text.
// wallpaper: a point clear of both the taskbar AND the terminal window
// (proc/apps/terminal.c, at (100,60)-(600,380) - (400,100) used to be a valid
// open-wallpaper point before that window existed and started covering
// it), must be the flat wallpaper color. taskbar_top/taskbar_bottom: the
// same x, first and last row of the 20px-tall taskbar - both must read
// the taskbar's own explicit background fill, proving no titlebar strip
// got drawn at the top (a titlebar bug would leave taskbar_top black -
// the window's untouched-cell default - while taskbar_bottom stayed
// correct).
static void cmd_desktop(void) {
    vga_print("desktop wallpaper=0x");
    serial_print("desktop wallpaper=0x");
    print_hex((u64) fb_get_pixel(700, 450));
    vga_print(" taskbar_top=0x");
    serial_print(" taskbar_top=0x");
    print_hex((u64) fb_get_pixel(400, 580));
    vga_print(" taskbar_bottom=0x");
    serial_print(" taskbar_bottom=0x");
    print_hex((u64) fb_get_pixel(400, 599));
    vga_print(" label_m_left=0x");
    serial_print(" label_m_left=0x");
    print_hex((u64) fb_get_pixel(8, 586));
    vga_print(" label_m_row1=0x");
    serial_print(" label_m_row1=0x");
    print_hex((u64) fb_get_pixel(8, 587));
}

// Resolves the gateway, resolves it again (cache hit), resolves the DNS
// proxy, and resolves an unreachable address (must fail cleanly).
static void cmd_arp(void) {
    ip_init();  // must run before reading g_gateway_ip/g_dns_server_ip below

    u8 mac[6];
    u64 t0 = g_tick_count;
    bool ok1 = arp_resolve(&g_gateway_ip[0], &mac[0]);
    u64 elapsed1 = g_tick_count - t0;
    vga_print("resolve gateway ok=0x");
    serial_print("resolve gateway ok=0x");
    print_hex((u64) ok1);
    if (ok1) {
        vga_print(" mac=");
        serial_print(" mac=");
        print_mac(&mac[0]);
    }
    vga_print(" elapsed_ticks=0x");
    serial_print(" elapsed_ticks=0x");
    print_hex(elapsed1);

    u8 mac2[6];
    u64 t1 = g_tick_count;
    bool ok2 = arp_resolve(&g_gateway_ip[0], &mac2[0]);
    u64 elapsed2 = g_tick_count - t1;
    vga_print(" cached_ok=0x");
    serial_print(" cached_ok=0x");
    print_hex((u64) ok2);
    vga_print(" cached_elapsed_ticks=0x");
    serial_print(" cached_elapsed_ticks=0x");
    print_hex(elapsed2);

    u8 mac3[6];
    bool ok3 = arp_resolve(&g_dns_server_ip[0], &mac3[0]);
    vga_print(" resolve_dns_proxy_ok=0x");
    serial_print(" resolve_dns_proxy_ok=0x");
    print_hex((u64) ok3);
    if (ok3) {
        vga_print(" dns_mac=");
        serial_print(" dns_mac=");
        print_mac(&mac3[0]);
    }

    u8 unreachable_ip[4];
    unreachable_ip[0] = 10;
    unreachable_ip[1] = 0;
    unreachable_ip[2] = 2;
    unreachable_ip[3] = 99;
    u8 mac4[6];
    bool ok4 = arp_resolve(&unreachable_ip[0], &mac4[0]);
    vga_print(" resolve_unreachable_ok=0x");
    serial_print(" resolve_unreachable_ok=0x");
    print_hex((u64) ok4);
}

// ping <host-or-ip> - a literal dotted-decimal IP is used as-is
// (parse_ip); anything else is treated as a hostname and resolved via
// the existing (previously unused by any command) dns_resolve_a(). Then
// 4 real ICMP echo requests (icmp_ping() genuinely sends/waits for a
// matching reply, not simulated), same real-shaped output every
// Linux/Windows ping gives: one line per reply (or a timeout), then a
// summary. There's no sub-tick timer here, and QEMU/TCG's real tick rate
// is unreliable (see CLAUDE.md) - the "ms" figure is real ticks × 10
// (nominal 100Hz), labeled "~" rather than presented as false precision.
#define PING_COUNT 4
#define PING_IDENTIFIER 0x1234
static void cmd_ping(void) {
    char* arg = &g_line_buffer[5];  // past "ping "
    if (arg[0] == '\0') {
        vga_print("usage: ping <host-or-ip>");
        serial_print("usage: ping <host-or-ip>");
        return;
    }

    u8 target_ip[4];
    if (!parse_ip(arg, target_ip)) {
        if (!dns_resolve_a(arg, target_ip)) {
            vga_print("ping: could not resolve ");
            serial_print("ping: could not resolve ");
            vga_print(arg);
            serial_print(arg);
            return;
        }
    }

    vga_print("PING ");
    serial_print("PING ");
    print_ip(target_ip);
    vga_print("  ");
    serial_print("  ");

    int received = 0;
    int seq = 1;
    while (seq <= PING_COUNT) {
        u64 start_tick = g_tick_count;
        bool ok = icmp_ping(target_ip, PING_IDENTIFIER, (u16) seq);
        u64 elapsed = g_tick_count - start_tick;

        if (ok) {
            received = received + 1;
            vga_print("12 bytes from ");
            serial_print("12 bytes from ");
            print_ip(target_ip);
            vga_print(": icmp_seq=");
            serial_print(": icmp_seq=");
            print_decimal((u64) seq);
            vga_print(" ttl=64 time=~");
            serial_print(" ttl=64 time=~");
            print_decimal(elapsed * 10);
            vga_print("ms  ");
            serial_print("ms  ");
        } else {
            vga_print("Request timeout for icmp_seq=");
            serial_print("Request timeout for icmp_seq=");
            print_decimal((u64) seq);
            vga_print("  ");
            serial_print("  ");
        }
        seq = seq + 1;
    }

    print_decimal((u64) PING_COUNT);
    vga_print(" transmitted, ");
    serial_print(" transmitted, ");
    print_decimal((u64) received);
    vga_print(" received");
    serial_print(" received");
}

static void cmd_ipconfig(void) {
    ip_init();  // must run before reading g_my_ip/g_gateway_ip/g_dns_server_ip

    vga_print("IP: ");
    serial_print("IP: ");
    print_ip(g_my_ip);
    vga_print("  GATEWAY: ");
    serial_print("  GATEWAY: ");
    print_ip(g_gateway_ip);
    vga_print("  DNS: ");
    serial_print("  DNS: ");
    print_ip(g_dns_server_ip);

    bool ok = e1000_init();
    if (!ok) {
        vga_print("  MAC: (e1000 not found)");
        serial_print("  MAC: (e1000 not found)");
        return;
    }
    u8 mac[6];
    e1000_get_mac(&mac[0]);
    vga_print("  MAC: ");
    serial_print("  MAC: ");
    print_mac(&mac[0]);
    vga_print("  LINK: ");
    serial_print("  LINK: ");
    vga_print(e1000_link_up() ? "UP" : "DOWN");
    serial_print(e1000_link_up() ? "UP" : "DOWN");
}

static void cmd_dns(void) {
    u64 start_tick = g_tick_count;
    bool ok = dns_query("example.com");
    u64 elapsed = g_tick_count - start_tick;

    vga_print("dns query ok=0x");
    serial_print("dns query ok=0x");
    print_hex((u64) ok);
    vga_print(" elapsed_ticks=0x");
    serial_print(" elapsed_ticks=0x");
    print_hex(elapsed);
}

static void cmd_tcp(void) {
    u8 ip[4];
    if (!dns_resolve_a("example.com", &ip[0])) {
        vga_print("tcp: could not resolve example.com");
        serial_print("tcp: could not resolve example.com");
        return;
    }
    vga_print("resolved example.com -> 0x");
    serial_print("resolved example.com -> 0x");
    print_hex(ip[0]);
    vga_print(".0x");
    serial_print(".0x");
    print_hex(ip[1]);
    vga_print(".0x");
    serial_print(".0x");
    print_hex(ip[2]);
    vga_print(".0x");
    serial_print(".0x");
    print_hex(ip[3]);

    const char* request = "GET / HTTP/1.1\r\nHost: example.com\r\nConnection: close\r\n\r\n";
    u8 response[512];
    u32 response_len = 0;
    u64 start_tick = g_tick_count;
    bool ok = tcp_fetch(&ip[0], 80, request, (u16) strlen_(request), &response[0], 512, &response_len);
    u64 elapsed = g_tick_count - start_tick;

    vga_print(" tcp_fetch_ok=0x");
    serial_print(" tcp_fetch_ok=0x");
    print_hex((u64) ok);
    vga_print(" response_len=0x");
    serial_print(" response_len=0x");
    print_hex((u64) response_len);
    vga_print(" elapsed_ticks=0x");
    serial_print(" elapsed_ticks=0x");
    print_hex(elapsed);

    bool got_http_status = response_len >= 4
        && response[0] == 'H' && response[1] == 'T' && response[2] == 'T' && response[3] == 'P';
    vga_print(" got_http_status=0x");
    serial_print(" got_http_status=0x");
    print_hex((u64) got_http_status);
}

static void cmd_echo(void) {
    char* text = &g_line_buffer[5];  // past "echo "
    vga_print(text);
    serial_print(text);
}

#pragma GCC visibility push(hidden)
extern u8 g_png_test_stored_start;
extern u8 g_png_test_stored_end;
extern u8 g_png_test_huffman_start;
extern u8 g_png_test_huffman_end;
extern u8 g_cursor_png_start;
extern u8 g_cursor_png_end;
#pragma GCC visibility pop

static void print_labeled_hex(const char* label, u64 value) {
    vga_print(label);
    serial_print(label);
    print_hex(value);
}

// Exercises the hand-written PNG decoder (kernel/gfx/png/png.c) against
// three real embedded PNGs and prints exact values to compare by hand:
// a tiny stored-DEFLATE-block PNG with hand-picked pixels, a 64x64
// dynamic-Huffman PNG whose pixels follow a known formula, and the real
// cursor.png asset - plus a deliberate one-byte corruption to prove the
// CRC32 check actually rejects bad input instead of decoding garbage.
static void cmd_pngtest(void) {
    u32 stored_size = (u32) ((u64) &g_png_test_stored_end - (u64) &g_png_test_stored_start);
    image stored_img;
    bool ok1 = png_decode(&g_png_test_stored_start, stored_size, &stored_img);
    print_labeled_hex("stored ok=0x", ok1 ? 1 : 0);
    if (ok1) {
        print_labeled_hex(" w=0x", stored_img.width);
        print_labeled_hex(" h=0x", stored_img.height);
        print_labeled_hex(" px0=0x", stored_img.pixels[0]);   // expect 0x00000000 (0,0,0)
        print_labeled_hex(" px1=0x", stored_img.pixels[1]);   // expect 0x00FF0000 (255,0,0)
        print_labeled_hex(" px15=0x", stored_img.pixels[15]); // expect 0x00FFFFFF (255,255,255)
    }

    u32 huff_size = (u32) ((u64) &g_png_test_huffman_end - (u64) &g_png_test_huffman_start);
    image huff_img;
    bool ok2 = png_decode(&g_png_test_huffman_start, huff_size, &huff_img);
    print_labeled_hex(" huffman ok=0x", ok2 ? 1 : 0);
    if (ok2) {
        print_labeled_hex(" w=0x", huff_img.width);
        print_labeled_hex(" h=0x", huff_img.height);
        u32 x = 37;
        u32 y = 50;
        u32 expect = (((x * 7) % 256) << 16) | (((y * 11) % 256) << 8) | (((x ^ y) * 3) % 256);
        print_labeled_hex(" px37_50=0x", huff_img.pixels[y * huff_img.width + x]);
        print_labeled_hex(" expect=0x", expect);
    }

    u32 cursor_size = (u32) ((u64) &g_cursor_png_end - (u64) &g_cursor_png_start);
    image cursor_img;
    bool ok3 = png_decode(&g_cursor_png_start, cursor_size, &cursor_img);
    print_labeled_hex(" cursor ok=0x", ok3 ? 1 : 0);
    if (ok3) {
        print_labeled_hex(" w=0x", cursor_img.width);
        print_labeled_hex(" h=0x", cursor_img.height);
        print_labeled_hex(" px0=0x", cursor_img.pixels[0]);           // expect 0x00000000 (black outline)
        print_labeled_hex(" px_transparent=0x", cursor_img.pixels[1]); // expect 0xFFFFFFFF
    }

    // Corrupt one byte inside the stored PNG's chunk data (CRC32 covers
    // the whole type+data span of every chunk, so any single-byte flip
    // past the 8-byte signature breaks some chunk's CRC) and confirm
    // png_decode rejects it instead of producing wrong pixels.
    if (stored_size <= 256) {
        u8 corrupt[256];
        for (u32 i = 0; i < stored_size; i = i + 1) {
            corrupt[i] = (&g_png_test_stored_start)[i];
        }
        corrupt[stored_size / 2] = corrupt[stored_size / 2] ^ 0xFF;
        image bad_img;
        bool ok4 = png_decode(corrupt, stored_size, &bad_img);
        print_labeled_hex(" corrupt_rejected=0x", ok4 ? 0 : 1); // expect 1
    }
}

void run_command(void) {
    if (streq(g_line_buffer, "help")) {
        cmd_help();
    } else if (streq(g_line_buffer, "clear")) {
        cmd_clear();
    } else if (streq(g_line_buffer, "ticks")) {
        cmd_ticks();
    } else if (streq(g_line_buffer, "alloc")) {
        cmd_alloc();
    } else if (streq(g_line_buffer, "bigalloc")) {
        cmd_big_alloc();
    } else if (streq(g_line_buffer, "free")) {
        cmd_free();
    } else if (starts_with(g_line_buffer, "free ")) {
        cmd_free_addr();
    } else if (streq(g_line_buffer, "mem")) {
        cmd_mem();
    } else if (streq(g_line_buffer, "reset")) {
        cmd_reset();
    } else if (streq(g_line_buffer, "exit")) {
        cmd_exit();
    } else if (streq(g_line_buffer, "ring3tcpserver")) {
        cmd_ring3_tcp_server();
    } else if (streq(g_line_buffer, "ring3widgets")) {
        cmd_ring3_widgets();
    } else if (streq(g_line_buffer, "ring3focus")) {
        cmd_ring3_focus();
    } else if (streq(g_line_buffer, "checkboxcontent")) {
        cmd_checkboxcontent();
    } else if (streq(g_line_buffer, "radiocontent")) {
        cmd_radiocontent();
    } else if (streq(g_line_buffer, "progresscontent")) {
        cmd_progresscontent();
    } else if (streq(g_line_buffer, "slidercontent")) {
        cmd_slidercontent();
    } else if (streq(g_line_buffer, "listcontent")) {
        cmd_listcontent();
    } else if (starts_with(g_line_buffer, "service ")) {
        cmd_service();
    } else if (streq(g_line_buffer, "shutdown")) {
        cmd_shutdown();
    } else if (streq(g_line_buffer, "reboot")) {
        cmd_reboot();
    } else if (streq(g_line_buffer, "cursor")) {
        cmd_cursor();
    } else if (streq(g_line_buffer, "frames")) {
        cmd_frames();
    } else if (streq(g_line_buffer, "frame")) {
        cmd_frame();
    } else if (streq(g_line_buffer, "unframe")) {
        cmd_unframe();
    } else if (streq(g_line_buffer, "map")) {
        cmd_map();
    } else if (streq(g_line_buffer, "tasks")) {
        cmd_tasks();
    } else if (streq(g_line_buffer, "procs")) {
        cmd_procs();
    } else if (streq(g_line_buffer, "chan")) {
        cmd_chan();
    } else if (streq(g_line_buffer, "send")) {
        cmd_send();
    } else if (streq(g_line_buffer, "ps")) {
        cmd_ps();
    } else if (streq(g_line_buffer, "objs")) {
        cmd_objs();
    } else if (streq(g_line_buffer, "netconns")) {
        cmd_netconns();
    } else if (streq(g_line_buffer, "disk")) {
        cmd_disk();
    } else if (streq(g_line_buffer, "diskwrite")) {
        cmd_disk_write();
    } else if (streq(g_line_buffer, "mkfs")) {
        cmd_mkfs();
    } else if (streq(g_line_buffer, "mkfile")) {
        cmd_mkfile();
    } else if (streq(g_line_buffer, "cat")) {
        cmd_cat();
    } else if (starts_with(g_line_buffer, "cat ")) {
        cmd_cat_path();
    } else if (streq(g_line_buffer, "ls")) {
        cmd_ls();
    } else if (streq(g_line_buffer, "pwd")) {
        cmd_pwd();
    } else if (streq(g_line_buffer, "cd")) {
        cmd_cd_root();
    } else if (starts_with(g_line_buffer, "cd ")) {
        cmd_cd();
    } else if (starts_with(g_line_buffer, "mkdir ")) {
        cmd_mkdir();
    } else if (starts_with(g_line_buffer, "cp ")) {
        cmd_cp();
    } else if (starts_with(g_line_buffer, "mv ")) {
        cmd_mv();
    } else if (starts_with(g_line_buffer, "touch ")) {
        cmd_touch();
    } else if (starts_with(g_line_buffer, "edit ")) {
        cmd_edit();
    } else if (starts_with(g_line_buffer, "vfscat ")) {
        cmd_vfs_cat();
    } else if (streq(g_line_buffer, "vfswrite")) {
        cmd_vfs_write();
    } else if (streq(g_line_buffer, "install")) {
        cmd_install();
    } else if (streq(g_line_buffer, "spawn")) {
        cmd_spawn();
    } else if (streq(g_line_buffer, "ring3go")) {
        cmd_ring3_go();
    } else if (streq(g_line_buffer, "ring3fault")) {
        cmd_ring3_fault();
    } else if (streq(g_line_buffer, "ring3nx")) {
        cmd_ring3_nx();
    } else if (streq(g_line_buffer, "ring3reg")) {
        cmd_ring3_register();
    } else if (streq(g_line_buffer, "ring3unreg")) {
        cmd_ring3_unregister();
    } else if (streq(g_line_buffer, "ring3async")) {
        cmd_ring3_async();
    } else if (streq(g_line_buffer, "ring3asyncwrite")) {
        cmd_ring3_async_write();
    } else if (streq(g_line_buffer, "ring3asyncping")) {
        cmd_ring3_async_ping();
    } else if (streq(g_line_buffer, "ring3asyncdns")) {
        cmd_ring3_async_dns();
    } else if (streq(g_line_buffer, "ring3asynctcp")) {
        cmd_ring3_async_tcp();
    } else if (streq(g_line_buffer, "ring3win")) {
        cmd_ring3_window();
    } else if (streq(g_line_buffer, "ring3mouse")) {
        cmd_ring3_mouse();
    } else if (streq(g_line_buffer, "ring3text")) {
        cmd_ring3_text();
    } else if (streq(g_line_buffer, "ring3fileobj")) {
        cmd_ring3_file_object();
    } else if (streq(g_line_buffer, "ring3perms")) {
        cmd_ring3_perms();
    } else if (streq(g_line_buffer, "ring3posix")) {
        cmd_ring3_posix();
    } else if (streq(g_line_buffer, "ring3pipe")) {
        cmd_ring3_pipe();
    } else if (streq(g_line_buffer, "ring3shm")) {
        cmd_ring3_shm();
    } else if (streq(g_line_buffer, "ring3button")) {
        cmd_ring3_button();
    } else if (streq(g_line_buffer, "pci")) {
        cmd_pci();
    } else if (streq(g_line_buffer, "devices")) {
        cmd_devices();
    } else if (streq(g_line_buffer, "nic")) {
        cmd_nic();
    } else if (streq(g_line_buffer, "fb")) {
        cmd_fb();
    } else if (streq(g_line_buffer, "text")) {
        cmd_text();
    } else if (streq(g_line_buffer, "mouse")) {
        cmd_mouse();
    } else if (streq(g_line_buffer, "win")) {
        cmd_win();
    } else if (streq(g_line_buffer, "winlist")) {
        cmd_winlist();
    } else if (streq(g_line_buffer, "wincontent")) {
        cmd_wincontent();
    } else if (streq(g_line_buffer, "textcontent")) {
        cmd_textcontent();
    } else if (streq(g_line_buffer, "buttoncontent")) {
        cmd_buttoncontent();
    } else if (streq(g_line_buffer, "desktop")) {
        cmd_desktop();
    } else if (streq(g_line_buffer, "arp")) {
        cmd_arp();
    } else if (starts_with(g_line_buffer, "ping ")) {
        cmd_ping();
    } else if (streq(g_line_buffer, "ipconfig")) {
        cmd_ipconfig();
    } else if (streq(g_line_buffer, "dns")) {
        cmd_dns();
    } else if (streq(g_line_buffer, "tcp")) {
        cmd_tcp();
    } else if (starts_with(g_line_buffer, "echo ")) {
        cmd_echo();
    } else if (streq(g_line_buffer, "pngtest")) {
        cmd_pngtest();
    } else if (g_line_len > 0) {
        vga_print("unknown command");
        serial_print("unknown command");
    }
}
