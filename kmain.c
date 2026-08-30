// Kernel entry point, reached from boot.s in 64-bit long mode with the
// first 1GB of physical memory identity-mapped.

#include "types.h"
#include "kernel/drivers/io/io.h"
#include "kernel/drivers/interrupts_init/interrupts_init.h"
#include "kernel/drivers/keyboard/keyboard.h"
#include "kernel/drivers/device_manager/device_manager.h"
#include "kernel/mm/frames/frames.h"
#include "kernel/mm/paging/paging.h"
#include "kernel/sched/task.h"
#include "proc/ipc/channel/channel.h"
#include "proc/ipc/pipe/pipe.h"
#include "proc/process.h"
#include "proc/ipc/io_request/io_request.h"
#include "proc/ipc/net_request/net_request.h"
#include "proc/ipc/net_tcp_request/net_tcp_request.h"
#include "kernel/fs/vfs/vfs.h"
#include "shell/shell/shell.h"
#include "shell/editor/editor.h"

#pragma GCC visibility push(hidden)
extern u8 g_test_prog_start;
extern u8 g_test_prog_end;
extern u8 g_init_prog_start;
extern u8 g_init_prog_end;
extern u8 g_desktop_shell_prog_start;
extern u8 g_desktop_shell_prog_end;
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
    device_manager_register("PS/2 Keyboard", DEVICE_CATEGORY_INPUT, 1);
    // RTC has no init function at all (kernel/drivers/rtc/rtc.c only ever
    // reads on demand) - registered as an assumed-always-present
    // platform device, matching how that driver itself never actually
    // probes for its own presence.
    device_manager_register("CMOS RTC", DEVICE_CATEGORY_PLATFORM, 0);

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
    g_ring3_pipe_demo = alloc_pipe();  // same well-known-index convention, its own separate namespace
    // stack_vaddr leaves 128KB past load_vaddr; the loaded image now spans more than
    // one page, so a smaller gap would collide the image and the stack.
    spawn_process(&g_test_prog_start, &g_test_prog_end, 0x80000000, 0x80020000);
    create_isolated_task(&proc_receiver_entry);
    vfs_mount("/system", BACKEND_MINIFS);
    vfs_mount("/devices", BACKEND_DEVICE);
    vfs_mount("/processes", BACKEND_PROCFS);

    // init process: spawns proc/demo/hello_service.c via spawn_builtin once running.
    spawn_process(&g_init_prog_start, &g_init_prog_end, 0x80000000, 0x80020000);

    // Desktop shell: wallpaper + taskbar + launcher, runs forever from
    // boot (not shell-triggered like ring3prog.c's demos) - activates the
    // framebuffer/graphics mode unconditionally on every boot from here on.
    // Terminal/File Manager/Settings are no longer auto-spawned here - the
    // taskbar's own MENU dropdown launches them on demand via syscall 41
    // (kernel/syscall/syscall.c's gui_app_bounds()), so only the shell
    // itself needs to exist at boot.
    spawn_process(&g_desktop_shell_prog_start, &g_desktop_shell_prog_end, 0x80000000, 0x80020000);

    __asm__ volatile("sti");

    serial_print("interrupts live\n");
    print_prompt();

    for (;;) {
        __asm__ volatile("hlt");
        if (g_line_ready) {
            run_command();
            g_line_ready = false;
            g_line_len = 0;
            g_line_cursor = 0;
            // A full-screen `edit` session (shell/editor.c) has already
            // taken over the display by the time cmd_edit() returns here -
            // reprinting a prompt on top of it would stomp the freshly-
            // drawn editor screen. editor_save_and_exit() prints the next
            // real prompt itself once the screen is normal shell output's
            // to draw on again.
            if (!g_editor_active) {
                new_line();
                print_prompt();
            }
        }
    }
}
