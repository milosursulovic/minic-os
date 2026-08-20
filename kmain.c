// The C side of the kernel. By the time boot.s's `call _start` reaches
// here, we're in 64-bit long mode with a real stack and the first 1GB of
// physical memory identity-mapped - including the VGA text buffer at
// 0xB8000, which needs no driver at all: it's just memory.
//
// Stage 1 of the C rewrite: boot + interrupts (IDT/PIC/PIT) + keyboard +
// a minimal shell skeleton. Memory management, the scheduler, syscalls/
// processes, disk/filesystem, and networking are later stages, added
// here incrementally as each one is ported.

#include "types.h"
#include "drivers/io.h"
#include "drivers/interrupts_init.h"
#include "drivers/keyboard.h"
#include "mm/frames.h"
#include "mm/paging.h"
#include "sched/task.h"
#include "proc/channel.h"
#include "proc/process.h"
#include "disk/vfs.h"
#include "shell/shell.h"

#pragma GCC visibility push(hidden)
extern u8 g_test_prog_start;
extern u8 g_test_prog_end;
extern u8 g_init_prog_start;
extern u8 g_init_prog_end;
#pragma GCC visibility pop

void _start(void) {
    volatile vga_char* vga = (volatile vga_char*) 0xB8000;
    const char* message = "Hello from a C kernel!";
    int i = 0;
    while (message[i] != '\0') {
        vga[i].character = (u8) message[i];
        vga[i].color = 0x0F;
        i = i + 1;
    }
    serial_print("Hello from a C kernel!\n");

    g_vga = (volatile vga_char*) 0xB8000;
    g_vga_cursor = 80;  // second row - leave the boot message on row 0
    init_scancode_table();

    idt_init();
    pic_remap();
    pit_init();
    frames_init();
    read_pml4();

    // Scheduler state must exist *before* interrupts are live - the timer
    // ISR calls yield() unconditionally now (preemption), and yield()
    // divides by g_task_count; a timer tick landing between `sti` and
    // scheduler_init() would hit g_task_count==0.
    scheduler_init();
    create_task(&task1_entry);
    create_task(&task2_entry);
    create_task(&task3_entry);
    create_task(&task4_entry);
    create_isolated_task(&proc_a_entry);
    create_isolated_task(&proc_b_entry);
    // g_channel_demo MUST be created first so it keeps channel index 0,
    // exactly as every existing test/transcript already assumes -
    // create_channel() just returns g_channel_count at the time of the
    // call, so creation ORDER is what fixes each channel's index, not
    // which global variable it's assigned to.
    g_channel_demo = create_channel();
    // A second, dedicated channel for the ring3 program's own
    // Channel.receive() call - index 1 by construction (the second
    // create_channel() call overall), created before spawn_process() so
    // it already exists the instant that task starts running.
    // Deliberately a *different* channel than g_channel_demo's - the
    // kernel-task IPC demo and this ring3 demo must never share one, or
    // the shell's `send` command and the spawn-trigger commands would
    // race on the same mailbox.
    g_ring3_channel_demo = create_channel();
    // Boot-time automatic spawn of the compiled-in ring3 test program -
    // stack_vaddr is 0x80020000 (128KB of headroom past load_vaddr),
    // not 0x80001000: the compiled program is now bigger than one page,
    // and a smaller gap would let the image's own second page and the
    // user stack collide on the same virtual address. Every spawn call
    // site (here, shell.c's cmd_spawn, ring3prog.c's own process_spawn()
    // call) must keep using this same stack_vaddr.
    spawn_process(&g_test_prog_start, &g_test_prog_end, 0x80000000, 0x80020000);
    create_isolated_task(&proc_receiver_entry);
    vfs_mount("/system", BACKEND_MINIFS);
    vfs_mount("/devices", BACKEND_DEVICE);

    // Milestone 36: the kernel's first real init process - a separate,
    // dedicated ring3 program (not the demo above) whose only job is to
    // spawn a real service of its own (proc/hello_service.c, via the
    // new spawn_builtin syscall) once it's running. Same fixed load/
    // stack addresses as every other spawn_process() call site here -
    // safe to reuse since this lands in its own private, cloned address
    // space, same as the demo process spawning a second instance of
    // itself already proved.
    spawn_process(&g_init_prog_start, &g_init_prog_end, 0x80000000, 0x80020000);

    __asm__ volatile("sti");

    serial_print("interrupts live\n");
    print_prompt();

    for (;;) {
        __asm__ volatile("hlt");
        if (g_line_ready) {
            run_command();
            g_line_ready = false;
            g_line_len = 0;
            new_line();
            print_prompt();
        }
    }
}
