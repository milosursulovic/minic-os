#include "ring3prog_common.h"

// Extracted from the former single ring3prog.c (1380 lines) - see that
// file's own header comment and ring3prog_common.h for the split rationale.
// Returns true if trigger_value matched one of this group's own triggers
// (and was therefore handled - some branches below never return at all,
// looping forever, exactly as in the original single-file version).
bool run_trigger_ipc(u64 trigger_value) {
    if (trigger_value == 19) {
        // trigger 19 (ring3shm): real frame-backed SharedMemory
        // (proc/ipc/shared_memory/shared_memory.h) - the same physical
        // frames mapped at two different virtual addresses in this
        // process, plus (structurally) into a freshly-spawned child's
        // address space.
        int shm_handle = gt_shm_create(8192);
        do_syscall(1, (u64) "shm_create() handle=0x", (u64) shm_handle, 0);

        u64 vaddr1 = 0x80300000;
        u64 vaddr2 = 0x80400000;
        bool mapped1 = gt_shm_map(shm_handle, vaddr1);
        do_syscall(1, (u64) "shm_map(vaddr1) ok=0x", (u64) mapped1, 0);

        u8* through1 = (u8*) vaddr1;
        const char* pattern = "shared memory works";
        int i = 0;
        while (pattern[i] != '\0') {
            through1[i] = (u8) pattern[i];
            i = i + 1;
        }
        through1[i] = 0;

        bool mapped2 = gt_shm_map(shm_handle, vaddr2);
        do_syscall(1, (u64) "shm_map(vaddr2) ok=0x", (u64) mapped2, 0);
        u8* through2 = (u8*) vaddr2;
        do_syscall(1, (u64) "read through vaddr2: ", 0, 0);
        do_syscall(1, (u64) through2, 0, 0);

        process child_image;
        child_image.path = "/system/testprog.bin";
        u64 child_task_index = process_spawn(&child_image, 0x80000000, 0x80020000);
        bool mapped_into_child = gt_shm_map_into(shm_handle, child_task_index, vaddr1);
        do_syscall(1, (u64) "shm_map_into(child) ok=0x", (u64) mapped_into_child, 0);
    } else if (trigger_value == 20) {
        // trigger 20 (ring3tcpserver): a real generic Socket object
        // (proc/ipc/socket/socket.h) - genuine TCP server listen/accept
        // over kernel/net/tcp/tcp.c's real server-side handshake, echoed
        // back to whatever real external client connects (see the
        // kernel-qemu-test hostfwd verification for this trigger).
        int listen_handle = gt_socket_listen(9000);
        do_syscall(1, (u64) "socket_listen(9000) handle=0x", (u64) listen_handle, 0);

        int conn_handle = gt_socket_accept(listen_handle);
        do_syscall(1, (u64) "socket_accept() handle=0x", (u64) conn_handle, 0);

        if (conn_handle >= 0) {
            int round = 0;
            while (round < 3) {
                u8 buf[128];
                int n = gt_socket_receive(conn_handle, buf, 127);
                if (n <= 0) {
                    do_syscall(1, (u64) "socket_receive() n=0x", (u64) n, 0);
                    round = 3;  // stop - client done or timed out
                } else {
                    buf[n] = 0;
                    do_syscall(1, (u64) "socket_receive() n=0x", (u64) n, 0);
                    do_syscall(1, (u64) &buf[0], 0, 0);
                    gt_socket_send(conn_handle, buf, (u16) n);  // echo back
                    round = round + 1;
                }
            }
            gt_socket_close(conn_handle);
        }
    } else if (trigger_value == 23) {
        // trigger 23 (ring3thread): real Thread object (syscalls 71-73,
        // kernel/sched/task.c's thread_join()) - a second task sharing
        // THIS process's own address space, not a whole new process.
        do_syscall(1, (u64) "counter before create=0x", g_thread_counter, 0);

        int thread_handle = gt_thread_create((u64) &thread_counter_entry);
        do_syscall(1, (u64) "gt_thread_create() handle=0x", (u64) thread_handle, 0);

        // Real race window, deliberately printed (not asserted) - the
        // spawned thread may or may not have run yet by this exact point,
        // same as any real concurrent scheduler. The join below is the
        // real synchronization point, not this line.
        do_syscall(1, (u64) "counter right after create=0x", g_thread_counter, 0);

        gt_thread_join(thread_handle);
        do_syscall(1, (u64) "counter after join=0x", g_thread_counter, 0);
    } else if (trigger_value == 24) {
        // trigger 24 (ring3sync): real Event/Mutex/Timer objects
        // (syscalls 74-82), built on trigger 23's own Thread object.
        g_sync_mutex_handle = gt_mutex_create();
        int t1 = gt_thread_create((u64) &sync_counter_entry);
        int t2 = gt_thread_create((u64) &sync_counter_entry);
        gt_thread_join(t1);
        gt_thread_join(t2);
        do_syscall(1, (u64) "mutex protected_counter=0x", g_protected_counter, 0);
        do_syscall(1, (u64) "mutex unprotected_counter=0x", g_unprotected_counter, 0);

        g_sync_event_handle = gt_event_create();
        g_event_work_done = false;
        int t3 = gt_thread_create((u64) &event_signal_entry);
        gt_event_wait(g_sync_event_handle);
        do_syscall(1, (u64) "event work_done after wait=0x", (u64) g_event_work_done, 0);
        gt_thread_join(t3);

        int timer_handle = gt_timer_create(50);
        u64 ticks_before = gt_get_ticks();
        gt_timer_wait(timer_handle);
        u64 ticks_after = gt_get_ticks();
        do_syscall(1, (u64) "timer ticks_before=0x", ticks_before, 0);
        do_syscall(1, (u64) "timer ticks_after=0x", ticks_after, 0);
        do_syscall(1, (u64) "timer elapsed=0x", ticks_after - ticks_before, 0);
    } else if (trigger_value == 25) {
        // trigger 25 (ring3shmsync, the parent/initiator role): real
        // cross-process SharedMemory, synchronized via Mutex/Event
        // (syscalls 74-82) and a new atomic 3-handle grant (syscall 84) -
        // Faza I point 8. Needs /system/testprog.bin to already exist
        // (run `install` first), same precondition trigger 19 already has.
        // The spawned child recognizes its own worker role via a pre-
        // check at the very top of THIS SAME _start() (see there for why
        // a channel-based handoff can't work here at all: channel_open()
        // only ever grants RIGHT_RECEIVE, by design, so ring3 code can
        // never successfully channel_send() to itself either).
        int shm_handle = gt_shm_create(4096);
        int event_handle = gt_event_create();
        int mutex_handle = gt_mutex_create();
        do_syscall(1, (u64) "shm_handle=0x", (u64) shm_handle, 0);
        do_syscall(1, (u64) "event_handle=0x", (u64) event_handle, 0);
        do_syscall(1, (u64) "mutex_handle=0x", (u64) mutex_handle, 0);

        process child_image;
        child_image.path = "/system/testprog.bin";
        u64 child_task_index = process_spawn(&child_image, 0x80000000, 0x80020000);
        do_syscall(1, (u64) "shmsync child_task_index=0x", child_task_index, 0);

        // Granted atomically - see syscall 84's own comment for why three
        // separate grant calls would leave a real, ring3-observable
        // partial-grant window. Lands deterministically at handle 1/2/3
        // (handle 0 = self, always granted first by spawn_process()) -
        // the child's own pre-check at the very top of _start() (before
        // it ever reaches its own file_write()) relies on this.
        bool granted = gt_handle_grant3((int) shm_handle, (int) event_handle, (int) mutex_handle, child_task_index);
        do_syscall(1, (u64) "granted child handles ok=0x", (u64) granted, 0);

        // Real synchronization point - blocks until the child has
        // actually written and signaled, not a fixed delay.
        gt_event_wait(event_handle);
        do_syscall(1, (u64) "parent observed child signal", 0, 0);

        bool mapped = gt_shm_map(shm_handle, SHM_SYNC_VADDR);
        do_syscall(1, (u64) "parent shm_map ok=0x", (u64) mapped, 0);
        do_syscall(1, (u64) "parent read from child: ", 0, 0);
        do_syscall(1, (u64) SHM_SYNC_VADDR, 0, 0);
    } else if (trigger_value == 27) {
        // trigger 27 (ring3msg) - Faza I point 8 item 4: a real
        // structured Channel payload, beyond one raw u64. channel_receive_full()
        // above already read the WHOLE message (trigger value + extra
        // payload) in one shot - g_msg_extra_len/g_msg_buf[8..] hold the
        // structured part the shell's ring3msg command appended after
        // the trigger value.
        char buf[RING3_MSG_BUF_MAX];
        u32 i = 0;
        while (i < g_msg_extra_len) {
            buf[i] = (char) g_msg_buf[8 + i];
            i = i + 1;
        }
        buf[g_msg_extra_len] = '\0';
        do_syscall(1, (u64) "ring3msg extra_len=0x", (u64) g_msg_extra_len, 0);
        do_syscall(1, (u64) "ring3msg payload: ", 0, 0);
        do_syscall(1, (u64) &buf[0], 0, 0);
    } else if (trigger_value == 28) {
        // trigger 28 (ring3objs) - Faza I point 2 item 5: real Directory/
        // Device object types, handle+rights gated (syscalls 87-92)
        // instead of the older raw dir_path+index (syscall 37) / raw
        // device index (syscall 64).
        int dir_handle = gt_directory_open("/system");
        do_syscall(1, (u64) "dir_handle=0x", (u64) dir_handle, 0);
        char entry_name[64];
        u32 entry_size;
        bool entry_is_dir;
        while (gt_directory_read_next(dir_handle, &entry_name[0], &entry_size, &entry_is_dir)) {
            do_syscall(1, (u64) "  entry: ", 0, 0);
            do_syscall(1, (u64) &entry_name[0], 0, 0);
            do_syscall(1, (u64) "    size=0x", (u64) entry_size, 0);
            do_syscall(1, (u64) "    is_dir=0x", (u64) entry_is_dir, 0);
        }
        gt_directory_close(dir_handle);
        do_syscall(1, (u64) "directory listing done", 0, 0);

        int device_handle = gt_device_open(0);
        do_syscall(1, (u64) "device_handle=0x", (u64) device_handle, 0);
        char device_name[32];
        int device_category;
        u32 device_info;
        bool queried = gt_device_query(device_handle, &device_name[0], &device_category, &device_info);
        do_syscall(1, (u64) "device_query ok=0x", (u64) queried, 0);
        do_syscall(1, (u64) "device name: ", 0, 0);
        do_syscall(1, (u64) &device_name[0], 0, 0);
        do_syscall(1, (u64) "device category=0x", (u64) device_category, 0);
        do_syscall(1, (u64) "device info=0x", (u64) device_info, 0);
        gt_device_close(device_handle);
    } else {
        return false;
    }
    return true;
}
