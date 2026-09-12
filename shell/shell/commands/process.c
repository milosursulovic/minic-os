#include "process.h"
#include "../../../kernel/drivers/io/io.h"
#include "../../../kernel/lib/strings.h"
#include "../../../kernel/isr/isr.h"
#include "../../../kernel/sched/task.h"
#include "../../../proc/process.h"
#include "../../../proc/ipc/object/object.h"
#include "../../../proc/ipc/channel/channel.h"
#include "../../../kernel/fs/vfs/vfs.h"
#include "../../../kernel/security/users/users.h"
#include "../../../kernel/security/exec_sign/exec_sign.h"

#pragma GCC visibility push(hidden)
extern u8 g_test_prog_start;
extern u8 g_test_prog_end;
#pragma GCC visibility pop

void cmd_tasks(void) {
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

void cmd_procs(void) {
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

void cmd_chan(void) {
    vga_print("receiver got: 0x");
    serial_print("receiver got: 0x");
    print_hex((u64) g_receiver_got_message);
    vga_print(" value=0x");
    serial_print(" value=0x");
    print_hex(g_receiver_value);
}

void cmd_send(void) {
    bool ok = channel_send(g_channel_demo, 0xC0FFEE1234);
    if (!ok) {
        vga_print("send failed - channel full");
        serial_print("send failed - channel full");
        return;
    }
    vga_print("sent 0xc0ffee1234");
    serial_print("sent 0xc0ffee1234");
}

void cmd_ps(void) {
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

void cmd_objs(void) {
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

// Faza I point 14, item 17: spawn_process_from_path() now refuses
// anything that doesn't carry a valid kernel/security/exec_sign/
// signature - self-sign this one builtin demo blob in-kernel (reusing
// the exact same exec_sign_produce()/hmac_sha256() the kernel's own
// verifier uses, no host tool needed for this path) so the existing
// install-then-spawn shell demo keeps working end-to-end through the
// new verification gate.
#define INSTALL_SIGN_BUF_SIZE 65536
static u8 g_install_sign_buf[INSTALL_SIGN_BUF_SIZE];

void cmd_install(void) {
    u32 len = (u32) ((u64) &g_test_prog_end - (u64) &g_test_prog_start);
    if (sizeof(exec_sign_header) + (u64) len > INSTALL_SIGN_BUF_SIZE) {
        vga_print("install failed - blob too large to sign");
        serial_print("install failed - blob too large to sign");
        return;
    }
    exec_sign_header header;
    exec_sign_produce(&g_test_prog_start, len, &header);
    u8* dst = &g_install_sign_buf[0];
    u32 i = 0;
    while (i < sizeof(exec_sign_header)) {
        dst[i] = ((u8*) &header)[i];
        i = i + 1;
    }
    u32 j = 0;
    while (j < len) {
        dst[sizeof(exec_sign_header) + j] = (&g_test_prog_start)[j];
        j = j + 1;
    }
    u32 signed_len = (u32) sizeof(exec_sign_header) + len;
    bool ok = vfs_write("/system/testprog.bin", dst, signed_len, 0);  // shell acts as root
    if (!ok) {
        vga_print("install failed");
        serial_print("install failed");
        return;
    }
    vga_print("installed /system/testprog.bin, 0x");
    serial_print("installed /system/testprog.bin, 0x");
    print_hex((u64) signed_len);
    vga_print(" bytes (signed)");
    serial_print(" bytes (signed)");
}

void cmd_spawn(void) {
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

// Real user-account table (Faza I point 14, kernel/security/users/) -
// lists every registered account, same introspection precedent as
// commands/hardware.c's cmd_devices/commands/service.c's cmd_service.
void cmd_users(void) {
    int i = 0;
    while (i < g_user_count) {
        vga_print(g_users[i].username);
        serial_print(g_users[i].username);
        vga_print(" uid=0x");
        serial_print(" uid=0x");
        print_hex((u64) g_users[i].uid);
        vga_print(" gid=0x");
        serial_print(" gid=0x");
        print_hex((u64) g_users[i].primary_gid);
        vga_print("  ");
        serial_print("  ");
        i = i + 1;
    }
}
