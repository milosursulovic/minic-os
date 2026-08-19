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
#include "../mm/heap.h"
#include "../mm/frames.h"
#include "../mm/paging.h"
#include "../sched/task.h"
#include "../proc/channel.h"
#include "../isr/isr.h"

void print_prompt(void) {
    vga_print("> ");
    serial_print("> ");
}

static void cmd_help(void) {
    vga_print("commands: help clear alloc bigalloc free free <addr> mem reset frame unframe frames map tasks procs chan send echo <text>");
    serial_print("commands: help clear alloc bigalloc free free <addr> mem reset frame unframe frames map tasks procs chan send echo <text>\n");
}

static void cmd_alloc(void) {
    void* p = kalloc(64);
    if (p == NULL) {
        vga_print("alloc failed - heap full");
        serial_print("alloc failed - heap full\n");
    } else {
        g_last_alloc = p;
        vga_print("allocated 64 bytes at 0x");
        serial_print("allocated 64 bytes at 0x");
        print_hex((u64) p);
    }
}

static void cmd_big_alloc(void) {
    // 64KB - bigger than a single heap_grow() chunk, so this reliably
    // forces at least one on-demand growth cycle in one shot, instead of
    // needing dozens of plain `alloc`s to exhaust the initial mapping.
    void* p = kalloc(65536);
    if (p == NULL) {
        vga_print("bigalloc failed - heap full");
        serial_print("bigalloc failed - heap full\n");
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
        serial_print("nothing to free\n");
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
    serial_print("heap reset\n");
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
        serial_print("out of frames\n");
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
        serial_print("nothing to unframe\n");
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
        serial_print("out of frames\n");
        return;
    }
    u64 vaddr = 0x40000000;  // 1GB - just past boot.s's static identity map
    bool ok = map_page(vaddr, (u64) frame, 0x02 | PAGE_NX);  // writable, non-executable
    if (!ok) {
        vga_print("map failed");
        serial_print("map failed\n");
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
        serial_print("send failed - channel full\n");
        return;
    }
    vga_print("sent 0xc0ffee1234");
    serial_print("sent 0xc0ffee1234\n");
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
    } else if (starts_with(g_line_buffer, "echo ")) {
        cmd_echo();
    } else if (g_line_len > 0) {
        vga_print("unknown command");
        serial_print("unknown command\n");
    }
}
