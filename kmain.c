// Kernel entry point, reached from boot.s in 64-bit long mode with the
// first 1GB of physical memory identity-mapped.

#include "types.h"
#include "drivers/io.h"
#include "drivers/interrupts_init.h"
#include "drivers/keyboard.h"
#include "mm/frames.h"
#include "mm/paging.h"
#include "sched/task.h"
#include "proc/channel.h"
#include "proc/process.h"
#include "proc/io_request.h"
#include "proc/net_request.h"
#include "proc/net_tcp_request.h"
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
    g_vga_cursor = 80;  // second row, leave the boot message on row 0
    vga_enable_cursor();
    vga_update_cursor(g_vga_cursor);
    init_scancode_table();

    idt_init();
    pic_remap();
    pit_init();
    frames_init();
    read_pml4();

    // Must run before `sti` - the timer ISR calls yield(), which divides by g_task_count.
    scheduler_init();
    create_task(&task1_entry);
    create_task(&task2_entry);
    create_task(&task3_entry);
    create_task(&task4_entry);
    create_isolated_task(&proc_a_entry);
    create_isolated_task(&proc_b_entry);
    create_task(&io_worker_entry);
    create_task(&net_worker_entry);
    create_task(&tcp_worker_entry);
    // Creation order fixes each channel's index (0, 1) - must stay in this order.
    g_channel_demo = create_channel();
    g_ring3_channel_demo = create_channel();
    // stack_vaddr leaves 128KB past load_vaddr; the loaded image now spans more than
    // one page, so a smaller gap would collide the image and the stack.
    spawn_process(&g_test_prog_start, &g_test_prog_end, 0x80000000, 0x80020000);
    create_isolated_task(&proc_receiver_entry);
    vfs_mount("/system", BACKEND_MINIFS);
    vfs_mount("/devices", BACKEND_DEVICE);

    // init process: spawns proc/hello_service.c via spawn_builtin once running.
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
