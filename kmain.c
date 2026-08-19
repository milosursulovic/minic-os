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
#include "shell/shell.h"

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
    create_isolated_task(&proc_receiver_entry);

    __asm__ volatile("sti");

    serial_print("interrupts live\n");
    print_prompt();

    for (;;) {
        __asm__ volatile("hlt");
        if (g_line_ready) {
            run_command();
            g_line_ready = false;
            g_line_len = 0;
            g_vga_cursor = ((g_vga_cursor / 80) + 1) * 80;  // next row
            if (g_vga_cursor >= 2000) {
                g_vga_cursor = 80;  // wrap - no real scrolling yet
            }
            print_prompt();
        }
    }
}
