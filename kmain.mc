// The MiniC side of the kernel. By the time boot.s's `call _start` reaches
// here, we're in 64-bit long mode with a real stack and the first 1GB of
// physical memory identity-mapped - including the VGA text buffer at
// 0xB8000, which needs no driver at all: it's just memory.
//
// Just the entry point now - everything else lives in its own module,
// one per subsystem (io/interrupts/keyboard/heap/frames/paging/strings/
// isr/shell). See README.md for the full milestone-by-milestone writeup
// of what each one does and why.

import "drivers/io.mc";
import "drivers/interrupts_init.mc";
import "drivers/keyboard.mc";
import "mm/frames.mc";
import "mm/paging.mc";
import "shell/shell.mc";
import "isr/isr.mc";
import "sched/task.mc";
import "syscall/syscall.mc";
import "proc/process.mc";
import "disk/ata.mc";
import "disk/minifs.mc";
import "disk/vfs.mc";

void _start() {
    volatile vga_char* vga = (volatile vga_char*) 0xB8000;
    char* message = "Hello from a MiniC kernel!";
    int i = 0;
    while (message[i] != '\0') {
        vga[i].character = message[i];
        vga[i].color = 0x0F;
        i = i + 1;
    }
    serial_print("Hello from a MiniC kernel!\n");

    g_vga = (volatile vga_char*) 0xB8000;
    g_vga_cursor = 80;   // second row - leave the boot message on row 0
    init_scancode_table();

    idt_init();
    pic_remap();
    pit_init();
    frames_init();
    read_pml4();

    // Scheduler state must exist *before* interrupts are live - the timer
    // ISR calls yield() unconditionally now (milestone 9's preemption),
    // and yield() divides by g_task_count; a timer tick landing between
    // `sti` and scheduler_init() would hit g_task_count==0.
    scheduler_init();
    create_task(&task1_entry);
    create_task(&task2_entry);
    create_task(&task3_entry);
    create_task(&task4_entry);
    create_isolated_task(&proc_a_entry);
    create_isolated_task(&proc_b_entry);
    // g_channel_demo (milestone 15) MUST be created first so it keeps
    // channel index 0, exactly as every existing test/transcript already
    // assumes - create_channel() just returns g_channel_count at the time
    // of the call, so creation ORDER is what fixes each channel's index,
    // not which global variable it's assigned to.
    g_channel_demo = create_channel();
    // Milestone 23: a second, dedicated channel for the ring3 program's
    // own Channel.receive() call - index 1 by construction (the second
    // create_channel() call overall), created before spawn_process() so it
    // already exists the instant that task starts running. Deliberately
    // a *different* channel than g_channel_demo's - the M15 kernel-task
    // demo and this ring3 demo must never share one, or the shell's
    // `send` command and this milestone's spawn-trigger command would
    // race on the same mailbox.
    g_ring3_channel_demo = create_channel();
    // Milestone 24: stack_vaddr moved from 0x80001000 to 0x80020000 (128KB
    // of headroom past load_vaddr) - a real bug found this milestone, not
    // a style change. The compiled ring3 image crossed 4096 bytes (one
    // page) for the first time here; at the old stack_vaddr, the image's
    // own second page and the user stack landed on the identical virtual
    // address, and whichever map_page_in() call ran second silently won,
    // leaving the other unreachable at that address. See proc/ring3prog.mc's
    // header comment for the full account. Every spawn_process()/
    // spawn_process_from_path() call site (here, shell.mc's cmd_spawn, and
    // ring3prog.mc's own Process.spawn() call) must keep using this same
    // stack_vaddr, not just this one - a mismatched constant would silently
    // reintroduce the exact same class of collision.
    spawn_process(&g_test_prog_start, &g_test_prog_end, 0x80000000, 0x80020000);
    create_isolated_task(&proc_receiver_entry);
    vfs_mount("/system", backend_minifs);
    vfs_mount("/devices", backend_device);
    asm("sti");

    serial_print("interrupts live\n");
    print_prompt();

    while (true) {
        asm("hlt");   // the CPU sleeps here between interrupts; every timer
        // tick preempts whatever's running (including this loop) via
        // interrupt_handler's own yield() call now - no manual yield()
        // needed here anymore, milestone 8's cooperative one was folded
        // into that automatic preemption.
        if (g_line_ready) {
            run_command();
            g_line_ready = false;
            g_line_len = 0;
            g_vga_cursor = ((g_vga_cursor / 80) + 1) * 80;   // next row
            if (g_vga_cursor >= 2000) {
                g_vga_cursor = 80;   // wrap - no real scrolling yet
            }
            print_prompt();
        }
    }
}
