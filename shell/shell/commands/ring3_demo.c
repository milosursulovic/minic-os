#include "ring3_demo.h"
#include "../../../kernel/drivers/io/io.h"
#include "../../../kernel/sched/task.h"
#include "../../../proc/ipc/channel/channel.h"
#include "../../../proc/ipc/pipe/pipe.h"

// Every command here just wakes the boot-time ring3 demo process
// (ring3prog.c, blocked on Channel.receive() since boot) with a
// distinct trigger value - see that file's own _start() for what each
// one actually does. `install` (commands/process.c) must run first for
// any trigger that spawns/refers to /system/testprog.bin.

// Wakes the boot-time ring3 process's blocked Channel.receive() -
// run `install` first so the file it spawns exists on disk.
void cmd_ring3_go(void) {
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
void cmd_ring3_fault(void) {
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
void cmd_ring3_nx(void) {
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
void cmd_ring3_register(void) {
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
void cmd_ring3_unregister(void) {
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
void cmd_ring3_async(void) {
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
void cmd_ring3_async_write(void) {
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
void cmd_ring3_async_ping(void) {
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
void cmd_ring3_async_dns(void) {
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
void cmd_ring3_async_tcp(void) {
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
void cmd_ring3_window(void) {
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
void cmd_ring3_mouse(void) {
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
void cmd_ring3_text(void) {
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
void cmd_ring3_button(void) {
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
void cmd_ring3_file_object(void) {
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
void cmd_ring3_perms(void) {
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
void cmd_ring3_posix(void) {
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
void cmd_ring3_pipe(void) {
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
void cmd_ring3_shm(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x13);
    if (!ok) {
        vga_print("ring3shm failed - channel full");
        serial_print("ring3shm failed - channel full");
        return;
    }
    vga_print("sent ring3 shared-memory trigger");
    serial_print("sent ring3 shared-memory trigger");
}

// Real generic Socket object over kernel/net/tcp/tcp.c's real TCP server
// listen/accept (see ring3prog.c trigger 20) - echoes back whatever a
// real external client sends, 3 rounds.
void cmd_ring3_tcp_server(void) {
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
void cmd_ring3_widgets(void) {
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
void cmd_ring3_focus(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x16);
    if (!ok) {
        vga_print("ring3focus failed - channel full");
        serial_print("ring3focus failed - channel full");
        return;
    }
    vga_print("sent ring3 focus trigger");
    serial_print("sent ring3 focus trigger");
}

// Real Thread object (Faza I point 3, syscalls 71-73) - see ring3prog.c
// trigger 23. Own trigger, not bundled into ring3widgets/ring3focus -
// this one's assertion is three printed counter snapshots in serial.log,
// unrelated to either of those triggers' own GUI/keyboard concerns.
void cmd_ring3_thread(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x17);
    if (!ok) {
        vga_print("ring3thread failed - channel full");
        serial_print("ring3thread failed - channel full");
        return;
    }
    vga_print("sent ring3 thread trigger");
    serial_print("sent ring3 thread trigger");
}

// Real Event/Mutex/Timer objects (syscalls 74-82) - see ring3prog.c
// trigger 24, built on trigger 23's own Thread object.
void cmd_ring3_sync(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x18);
    if (!ok) {
        vga_print("ring3sync failed - channel full");
        serial_print("ring3sync failed - channel full");
        return;
    }
    vga_print("sent ring3 sync trigger");
    serial_print("sent ring3 sync trigger");
}

// Real cross-process SharedMemory round-trip (syscall 83 handle_grant) -
// see ring3prog.c triggers 25 (parent)/26 (child). Needs /system/
// testprog.bin to already exist - run `install` first.
void cmd_ring3_shm_sync(void) {
    bool ok = channel_send(g_ring3_channel_demo, 0x19);
    if (!ok) {
        vga_print("ring3shmsync failed - channel full");
        serial_print("ring3shmsync failed - channel full");
        return;
    }
    vga_print("sent ring3 shmsync trigger");
    serial_print("sent ring3 shmsync trigger");
}

// Faza I point 8 item 4: proves a real structured Channel payload beyond
// one raw u64 - see ring3prog.c trigger 27. A channel is a single-slot
// mailbox (channel.c's own top comment) - a second channel_send_msg()
// right after the trigger send would just fail with "full" (real bug
// hit and fixed while building this test), since ring3 hasn't drained
// the first message yet. So the trigger value and the extra structured
// payload travel together as ONE message: first 8 bytes = the trigger
// value (u64, matching every existing trigger's own 8-byte convention),
// followed by the real payload bytes. ring3prog.c's _start() now always
// reads the full structured message (see its own channel_receive_full())
// and treats anything past the first 8 bytes as trigger-specific extra
// data - every existing trigger is unaffected since it only ever sends
// exactly 8 bytes.
void cmd_ring3_msg(void) {
    const char* payload = "structured-message-27-bytes!";
    u32 payload_len = 28;
    u8 combined[8 + 28];
    u64 trigger = 27;
    u8* trigger_bytes = (u8*) &trigger;
    u32 i = 0;
    while (i < 8) {
        combined[i] = trigger_bytes[i];
        i = i + 1;
    }
    i = 0;
    while (i < payload_len) {
        combined[8 + i] = (u8) payload[i];
        i = i + 1;
    }
    bool ok = channel_send_msg(g_ring3_channel_demo, &combined[0], 8 + payload_len);
    if (!ok) {
        vga_print("ring3msg failed - channel full");
        serial_print("ring3msg failed - channel full");
        return;
    }
    vga_print("sent ring3 msg trigger + structured payload");
    serial_print("sent ring3 msg trigger + structured payload");
}

// Faza I point 2 item 5: real Directory/Device object types - see
// ring3prog.c trigger 28.
void cmd_ring3_objs(void) {
    bool ok = channel_send(g_ring3_channel_demo, 28);
    if (!ok) {
        vga_print("ring3objs failed - channel full");
        serial_print("ring3objs failed - channel full");
        return;
    }
    vga_print("sent ring3 objs trigger");
    serial_print("sent ring3 objs trigger");
}

// Faza I point 14 item 6: real user-account lookup - see ring3prog.c
// trigger 29. Deliberately independent of ring3perms (16) - doesn't
// touch gt_setuid at all.
void cmd_ring3_users(void) {
    bool ok = channel_send(g_ring3_channel_demo, 29);
    if (!ok) {
        vga_print("ring3users failed - channel full");
        serial_print("ring3users failed - channel full");
        return;
    }
    vga_print("sent ring3 users trigger");
    serial_print("sent ring3 users trigger");
}

// Faza I point 5 item 7: proves the older raw vfs_read path is really
// permission-gated now, not just the File-object path ring3perms (16)
// already covers - see ring3prog.c trigger 30. Needs `install` (or a
// prior `mkfs`) the same as ring3perms already implicitly needs a real
// MiniFS volume mounted.
void cmd_ring3_vfs_perm(void) {
    bool ok = channel_send(g_ring3_channel_demo, 30);
    if (!ok) {
        vga_print("ring3vfsperm failed - channel full");
        serial_print("ring3vfsperm failed - channel full");
        return;
    }
    vga_print("sent ring3 vfsperm trigger");
    serial_print("sent ring3 vfsperm trigger");
}

// Faza I point 4 item 8: real copy-on-write fork() - see ring3prog.c
// trigger 31.
void cmd_ring3_fork(void) {
    bool ok = channel_send(g_ring3_channel_demo, 31);
    if (!ok) {
        vga_print("ring3fork failed - channel full");
        serial_print("ring3fork failed - channel full");
        return;
    }
    vga_print("sent ring3 fork trigger");
    serial_print("sent ring3 fork trigger");
}

// Faza I point 4 item 8: real guard pages - see ring3prog.c trigger 32.
// KERNEL-HALTING, run standalone.
void cmd_ring3_guard(void) {
    bool ok = channel_send(g_ring3_channel_demo, 32);
    if (!ok) {
        vga_print("ring3guard failed - channel full");
        serial_print("ring3guard failed - channel full");
        return;
    }
    vga_print("sent ring3 guard trigger - expect a page fault");
    serial_print("sent ring3 guard trigger - expect a page fault");
}

// Faza I point 3 item 9: real high-level Process.spawn()/.wait() native
// API - see ring3prog.c trigger 33.
void cmd_ring3_wait(void) {
    bool ok = channel_send(g_ring3_channel_demo, 33);
    if (!ok) {
        vga_print("ring3wait failed - channel full");
        serial_print("ring3wait failed - channel full");
        return;
    }
    vga_print("sent ring3 wait trigger");
    serial_print("sent ring3 wait trigger");
}
