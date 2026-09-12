// Kernel entry point, reached from boot.s in 64-bit long mode with the
// first 1GB of physical memory identity-mapped.

#include "types.h"
#include "kernel/drivers/io/io.h"
#include "kernel/drivers/interrupts_init/interrupts_init.h"
#include "kernel/drivers/keyboard/keyboard.h"
#include "kernel/drivers/device_manager/device_manager.h"
#include "kernel/drivers/usb/uhci.h"
#include "kernel/drivers/usb/usb_hid.h"
#include "kernel/mm/frames/frames.h"
#include "kernel/mm/paging/paging.h"
#include "kernel/sched/task.h"
#include "proc/ipc/channel/channel.h"
#include "proc/ipc/pipe/pipe.h"
#include "proc/process.h"
#include "proc/ipc/io_request/io_request.h"
#include "proc/ipc/net_request/net_request.h"
#include "proc/ipc/net_tcp_request/net_tcp_request.h"
#include "kernel/services/service_manager.h"
#include "kernel/security/users/users.h"
#include "kernel/lib/rand.h"
#include "kernel/fs/vfs/vfs.h"
#include "kernel/fs/minifs/minifs.h"
#include "kernel/fs/fat32/fat32.h"
#include "proc/ipc/object/object.h"
#include "kernel/drivers/io_port_range/io_port_range.h"
#include "shell/shell/shell.h"
#include "shell/editor/editor.h"

#pragma GCC visibility push(hidden)
extern u8 g_test_prog_start;
extern u8 g_test_prog_end;
extern u8 g_init_prog_start;
extern u8 g_init_prog_end;
extern u8 g_desktop_shell_prog_start;
extern u8 g_desktop_shell_prog_end;
extern u8 g_hello_service_prog_start;
extern u8 g_hello_service_prog_end;
extern u8 g_rtc_driver_prog_start;
extern u8 g_rtc_driver_prog_end;
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
    users_init();
    device_manager_register("PS/2 Keyboard", DEVICE_CATEGORY_INPUT, 1);
    // RTC has no init function at all (kernel/drivers/rtc/rtc.c only ever
    // reads on demand) - registered as an assumed-always-present
    // platform device, matching how that driver itself never actually
    // probes for its own presence.
    device_manager_register("CMOS RTC", DEVICE_CATEGORY_PLATFORM, 0);

    // Real hand-written UHCI USB driver (Faza I point 9, item 16) - a
    // real, honest no-op if no UHCI controller exists, or nothing is
    // attached to its root hub, matching every other driver's own
    // "lazily reflects what's really there" convention.
    if (uhci_init()) {
        usb_hid_init();
    }

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
    create_task(&service_manager_worker_entry);
    // Creation order fixes each channel's index (0, 1) - must stay in this order.
    g_channel_demo = create_channel();
    g_ring3_channel_demo = create_channel();
    g_ring3_pipe_demo = alloc_pipe();  // same well-known-index convention, its own separate namespace
    // Deliberately NOT randomized (real ASLR - kernel/lib/rand.h - covers
    // every other spawn site below): ring3prog.c is a fixed-address-
    // anchored test harness, not an ordinary program - several of its own
    // triggers hardcode absolute addresses relative to this exact base
    // (ring3nx's 0x80020000 stack-execution target, ring3shm's
    // 0x80300000/0x80400000 mapping test, its own child-spawn's
    // 0x80000000/0x80020000 via syscall 6) - randomizing this spawn was
    // tried and found to break ring3nx for real (its hardcoded stack
    // address stopped being its actual stack, turning a real NX fault
    // - error_code=0x15 - into a not-present fault - error_code=0x6 -
    // silently testing the wrong thing). stack_vaddr keeps the same
    // 128KB gap every call site here already relied on.
    spawn_process(&g_test_prog_start, &g_test_prog_end, 0x80000000, 0x80020000, false);
    create_isolated_task(&proc_receiver_entry);
    vfs_mount("/system", BACKEND_MINIFS);
    vfs_mount("/devices", BACKEND_DEVICE);
    vfs_mount("/processes", BACKEND_PROCFS);

    // Faza I point 6, item 12: the remaining real mounts. /temp is a
    // genuinely ephemeral RAM-backed tmpfs (kernel/fs/tmpfs); /volumes
    // is an alias view of this same mount table (kernel/fs/vfs.c's
    // BACKEND_MOUNTS, no real second disk exists to represent
    // otherwise); /apps and /users are real MiniFS subdirectories
    // (fs_create_dir is already idempotent - fails harmlessly if the
    // name exists, same "ok if it didn't exist yet" pattern editor.c/
    // settings.c already rely on, safe to call every boot).
    vfs_mount("/temp", BACKEND_TMPFS);
    vfs_mount("/volumes", BACKEND_MOUNTS);
    vfs_mount_at("/apps", BACKEND_MINIFS, "apps");
    fs_create_dir("apps");
    vfs_mount_at("/users", BACKEND_MINIFS, "users");
    fs_create_dir("users");
    // Real per-user home directories for whichever accounts actually
    // exist (users_init() above seeded root/guest) - not hardcoded
    // names, so this stays correct if user_create() adds more later.
    int users_i = 0;
    while (users_i < MAX_USERS) {
        if (g_users[users_i].used) {
            char home_path[64];
            int p = 0;
            const char* prefix = "users/";
            while (prefix[p] != '\0') {
                home_path[p] = prefix[p];
                p = p + 1;
            }
            int q = 0;
            while (g_users[users_i].username[q] != '\0') {
                home_path[p] = g_users[users_i].username[q];
                p = p + 1;
                q = q + 1;
            }
            home_path[p] = '\0';
            fs_create_dir(home_path);
        }
        users_i = users_i + 1;
    }

    // Faza I point 6, item 15: a real, separate hand-written FAT32
    // driver (kernel/fs/fat32) on its own drive (kernel/fs/ata's new
    // drive-select support, drive 1 = slave) - proves the VFS backend
    // dispatch is genuinely pluggable, not hardcoded to MiniFS.
    fat32_init(1);
    vfs_mount("/fat32", BACKEND_FAT32);

    // Registered (available to "service start hello_service"), not
    // auto-started - real service-manager semantics, matches init.c's own
    // separate hardcoded demo below staying untouched.
    service_register("helloservice", &g_hello_service_prog_start, &g_hello_service_prog_end, true);

    // init process: spawns proc/demo/hello_service.c via spawn_builtin once running.
    u64 init_load_vaddr = randomize_load_vaddr(0x80000000);
    spawn_process(&g_init_prog_start, &g_init_prog_end, init_load_vaddr, init_load_vaddr + 0x20000, false);

    // Faza I point 14, item 14: real ring3 driver isolation proof of
    // concept - the CMOS RTC's actual port I/O now happens in ring3, not
    // kernel code (kernel/drivers/rtc/rtc.c stays as it is, used only for
    // kernel/lib/rand.c's own tiny early-boot ASLR seed - an explicit,
    // documented exception, not a contradiction: no ring3 process could
    // exist yet at that point). Two channels for the request/response
    // protocol; proc/drivers/rtc_driver/rtc_driver.c's own top comment
    // has the full fixed-handle-layout story. Wired up here, before sti,
    // the same no-preemption-yet window spawn_process()'s own
    // handle-0-self wiring already relies on being atomic.
    int rtc_request_channel = create_channel();
    int rtc_response_channel = create_channel();
    u64 rtc_driver_load_vaddr = randomize_load_vaddr(0x80000000);
    int rtc_driver_proc = spawn_process(&g_rtc_driver_prog_start, &g_rtc_driver_prog_end,
                                         rtc_driver_load_vaddr, rtc_driver_load_vaddr + 0x20000, false);
    if (rtc_driver_proc >= 0) {
        int io_slot = io_port_range_create(0x70, 0x71);
        int io_obj = alloc_object(OBJ_IO_PORT_RANGE, io_slot);
        alloc_handle(rtc_driver_proc, io_obj, RIGHT_READ | RIGHT_WRITE);  // handle 1
        int req_obj = alloc_object(OBJ_CHANNEL, rtc_request_channel);
        alloc_handle(rtc_driver_proc, req_obj, RIGHT_RECEIVE);            // handle 2
        int resp_obj = alloc_object(OBJ_CHANNEL, rtc_response_channel);
        alloc_handle(rtc_driver_proc, resp_obj, RIGHT_SEND);              // handle 3
    }

    // Desktop shell: wallpaper + taskbar + launcher, runs forever from
    // boot (not shell-triggered like ring3prog.c's demos) - activates the
    // framebuffer/graphics mode unconditionally on every boot from here on.
    // Terminal/File Manager/Settings are no longer auto-spawned here - the
    // taskbar's own MENU dropdown launches them on demand via syscall 41
    // (kernel/syscall/syscall.c's gui_app_bounds()), so only the shell
    // itself needs to exist at boot.
    u64 desktop_shell_load_vaddr = randomize_load_vaddr(0x80000000);
    int desktop_shell_proc = spawn_process(&g_desktop_shell_prog_start, &g_desktop_shell_prog_end,
                  desktop_shell_load_vaddr, desktop_shell_load_vaddr + 0x20000, false);
    if (desktop_shell_proc >= 0) {
        // gt_get_time()/gt_get_date() (proc/gui_toolkit/system.h) assume
        // exactly this handle layout - handle 1 = request (send), handle
        // 2 = response (receive). Only desktop_shell gets it; a general
        // any-process broker is out of scope for this single-driver proof
        // of concept (see system.h's own comment).
        int req_obj2 = alloc_object(OBJ_CHANNEL, rtc_request_channel);
        alloc_handle(desktop_shell_proc, req_obj2, RIGHT_SEND);     // handle 1
        int resp_obj2 = alloc_object(OBJ_CHANNEL, rtc_response_channel);
        alloc_handle(desktop_shell_proc, resp_obj2, RIGHT_RECEIVE); // handle 2
    }

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
