// Ring3 test program, compiled standalone (see ring3.ld) and loaded via
// spawn_process(). Exercises the File/Channel/Process/ProcessHandle API
// and a thin POSIX shim.
//
// _start must be at offset 0 of the loaded image - `__attribute__((section(".text.start")))`
// plus ring3.ld's ".text.start" force that regardless of gcc's own
// function ordering.

#include "ring3prog_common.h"

// Trigger dispatch bodies live in the sibling ring3prog_{security,io,gui,ipc}.c
// files (this file used to be 1380 lines, all of them in one place - see
// ring3prog_common.h's own comment for why it's split now). Each group
// function owns a disjoint subset of trigger numbers and returns false for
// anything outside its own set, so this dispatch is a plain fallthrough try.
bool run_trigger_security(u64 trigger_value);
bool run_trigger_io(u64 trigger_value);
bool run_trigger_gui(u64 trigger_value);
bool run_trigger_ipc(u64 trigger_value);

// The one real (non-duplicated) instance of these two - see
// ring3prog_common.h's own comment. Populated once by channel_receive_full()
// below; trigger 27 (ring3prog_ipc.c) reads them back.
u8 g_msg_buf[RING3_MSG_BUF_MAX];
u32 g_msg_extra_len;

__attribute__((section(".text.start")))
void _start(void) {
    // Faza I point 14, item 17: an always-on, passive sandbox self-report
    // - every fresh cold start of this binary (never a fork()/COW
    // continuation, which resumes mid-function, not here) probes syscall
    // 49 (setuid to its own current default, uid 0 - harmless either way)
    // and prints the raw result. kmain.c's own boot spawn of this exact
    // blob (spawn_process(..., sandboxed=false)) is unrestricted, so this
    // prints 0x0 (allowed) there; the SAME binary spawned later via the
    // shell's install-then-spawn demo (proc/process.c's
    // spawn_process_from_path(), sandboxed=true after passing signature
    // verification) prints 0xffffffffffffffff (denied) instead - real,
    // observable proof the deny-bitmap actually fires, not just that
    // OBJ_SANDBOX exists.
    u64 sandbox_probe = do_syscall(49, 0, 0, 0);
    do_syscall(1, (u64) "sandbox_setuid_probe (0x0=unsandboxed, -1=denied): 0x", sandbox_probe, 0);

    // Real cross-process SharedMemory round-trip child-role pre-check
    // (Faza I point 8, trigger 25) - if trigger 25's own parent already
    // atomically granted this fresh instance handles 1/2/3 (SharedMemory/
    // Event/Mutex, syscall 84) before it ever ran a single instruction,
    // THIS is that demo's own worker child, not a normal boot/trigger
    // instance - run the worker role immediately and exit, skipping the
    // entire normal channel-based dispatch below (which this instance
    // could never signal itself out of anyway - channel_open() only ever
    // grants RIGHT_RECEIVE, by design, so a would-be ring3-side
    // "send myself the next trigger" approach is a dead end). Handle 1
    // (not 2 - a real bug found and fixed here) is the first free slot:
    // this check runs BEFORE file_write()'s own ring3msg.txt handle
    // further down _start(), so nothing has consumed handle 1 yet at
    // this point - only handle 0 (self) exists before this. Syscall 84's
    // own atomicity guarantees handle 1 is either valid together with
    // 2/3, or not valid at all - never a partial, racy state visible here.
    // Real, found-empirically SEPARATE race: syscall 84's own grant is
    // atomic, but nothing stops THIS freshly-spawned task from being
    // scheduled and reaching this exact check BEFORE the parent's grant
    // call has run at all - a brand-new task is immediately runnable the
    // instant process_spawn() returns, and preemption can land here in
    // the small handful of instructions between that return and the
    // parent's very next syscall. A fixed retry COUNT turned out not to
    // be a real fix - under QEMU/TCG a few hundred cheap syscalls in a
    // tight loop can easily complete inside a single timer tick, so the
    // whole retry loop can run to exhaustion without a single real
    // preemption ever happening, never actually giving the parent a
    // turn. Bounding by real elapsed TICKS instead (same "throttle
    // window" convention this codebase already uses elsewhere) - ticks
    // only advance via genuine timer interrupts, which are also exactly
    // what hands other tasks their own turn, so this really does
    // guarantee real wall-clock opportunities for the parent's grant
    // call to run, not just more iterations of the same instant.
    bool shmsync_child_check = false;
    u64 shmsync_start_tick = gt_get_ticks();
    while (gt_get_ticks() - shmsync_start_tick < 50) {
        shmsync_child_check = gt_shm_map(1, SHM_SYNC_VADDR);
        if (shmsync_child_check) {
            break;
        }
    }
    if (shmsync_child_check) {
        gt_mutex_lock(3);
        char* dst = (char*) SHM_SYNC_VADDR;
        const char* payload = "hello from child process, synchronized!";
        int i = 0;
        while (payload[i] != '\0') {
            dst[i] = payload[i];
            i = i + 1;
        }
        dst[i] = '\0';
        gt_mutex_unlock(3);

        do_syscall(1, (u64) "child wrote payload, signaling", 0, 0);
        gt_event_signal(2);

        do_syscall(12, 0, 0, 0);  // process_exit - never returns
        for (;;) {
        }
    }

    do_syscall(3, 0, 0, 0);      // handle 0 = myself
    do_syscall(3, 99, 0, 0);     // handle 99 was never allocated - expect -1

    file msg_file;
    msg_file.path = "/system/ring3msg.txt";
    file_write(&msg_file, "hello from ring3, via a real File.write() method call!", 54);
    u64 read_back = file_read(&msg_file, (char*) &g_read_buf[0], 63);
    g_read_buf[read_back] = 0;

    int wfd = posix_open("/system/posix.txt", 1);
    posix_write(wfd, "POSIX ", 6);
    posix_write(wfd, "shim works!", 11);
    posix_close(wfd);

    int rfd = posix_open("/system/posix.txt", 0);
    char posix_buf1[8];
    int n1 = posix_read(rfd, &posix_buf1[0], 6);
    posix_buf1[n1] = 0;
    char posix_buf2[16];
    int n2 = posix_read(rfd, &posix_buf2[0], 11);
    posix_buf2[n2] = 0;
    posix_close(rfd);

    // channel index 1 - matches kmain.c's create_channel() order.
    // Unauthorized send() below is expected to fail (RIGHT_RECEIVE only).
    channel spawn_trigger;
    channel_open(&spawn_trigger, 1);
    channel_send(&spawn_trigger, 0xDEADBEEF);

    u64 trigger_value = channel_receive_full(&spawn_trigger);
    do_syscall(1, (u64) "Channel.receive() got trigger 0x", trigger_value, 0);


    if (!run_trigger_security(trigger_value)
        && !run_trigger_io(trigger_value)
        && !run_trigger_gui(trigger_value)
        && !run_trigger_ipc(trigger_value)) {
        process child_image;
        child_image.path = "/system/testprog.bin";
        u64 child_task_index = process_spawn(&child_image, 0x80000000, 0x80020000);
        do_syscall(1, (u64) "Process.spawn() launched task_index 0x", child_task_index, 0);

        if (child_task_index != (u64) -1) {
            process_handle no_rights;
            bool opened_no_rights = process_handle_open(&no_rights, (int) child_task_index, 0);
            do_syscall(1, (u64) "ProcessHandle.open(rights=0) ok=0x", (u64) opened_no_rights, 0);
            u64 unauthorized_query = process_handle_query(&no_rights);
            do_syscall(1, (u64) "unauthorized ProcessHandle.query() got 0x", unauthorized_query, 0);

            process_handle query_rights;
            bool opened_query = process_handle_open(&query_rights, (int) child_task_index, RIGHT_QUERY);
            do_syscall(1, (u64) "ProcessHandle.open(RIGHT_QUERY) ok=0x", (u64) opened_query, 0);
            u64 authorized_query = process_handle_query(&query_rights);
            do_syscall(1, (u64) "authorized ProcessHandle.query() got task_index 0x", authorized_query, 0);
        }
    }

    for (;;) {
    }
}
