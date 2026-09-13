#include "ring3prog_common.h"

// Extracted from the former single ring3prog.c (1380 lines) - see that
// file's own header comment and ring3prog_common.h for the split rationale.
// Returns true if trigger_value matched one of this group's own triggers
// (and was therefore handled - some branches below never return at all,
// looping forever, exactly as in the original single-file version).
bool run_trigger_security(u64 trigger_value) {
    // trigger 2 (ring3fault): forbidden write to kernel space - must page fault.
    if (trigger_value == 2) {
        do_syscall(1, (u64) "attempting forbidden ring3 write to 0x", 0x100000, 0);
        u64* forbidden = (u64*) 0x100000;
        *forbidden = 0xDEADBEEF;
        do_syscall(1, (u64) "forbidden write succeeded (BUG!) at 0x", 0x100000, 0);
    } else if (trigger_value == 3) {
        // trigger 3 (ring3nx): execute a byte on the user stack - must NX fault.
        do_syscall(1, (u64) "attempting to execute ring3 stack byte at 0x", 0x80020000, 0);
        u8* stack_code = (u8*) 0x80020000;
        *stack_code = 0xC3;
        void (*fn)(void) = (void (*)(void)) stack_code;
        fn();
        do_syscall(1, (u64) "stack execution succeeded (BUG!) at 0x", 0x80020000, 0);
    } else if (trigger_value == 4) {
        // trigger 4 (ring3reg): register testprog.bin at runtime, spawn it by index.
        u64 service_index = do_syscall(14, (u64) "/system/testprog.bin", 0, 0);
        do_syscall(1, (u64) "register_service() got index 0x", service_index, 0);

        u64 spawned_task_index = do_syscall(11, service_index, 0, 0);
        do_syscall(1, (u64) "spawn_builtin(registered) launched task_index 0x", spawned_task_index, 0);
    } else if (trigger_value == 5) {
        // trigger 5 (ring3unreg): register, unregister, confirm the slot is
        // freed (spawn_builtin fails), then register again - real reuse.
        u64 first_index = do_syscall(14, (u64) "/system/testprog.bin", 0, 0);
        do_syscall(1, (u64) "register_service() got index 0x", first_index, 0);

        u64 unreg_result = do_syscall(15, first_index, 0, 0);
        do_syscall(1, (u64) "unregister_service() result 0x", unreg_result, 0);

        u64 failed_spawn = do_syscall(11, first_index, 0, 0);
        do_syscall(1, (u64) "spawn_builtin(unregistered) got 0x", failed_spawn, 0);

        u64 second_index = do_syscall(14, (u64) "/system/testprog.bin", 0, 0);
        do_syscall(1, (u64) "register_service() again got index 0x", second_index, 0);

        u64 reused_task_index = do_syscall(11, second_index, 0, 0);
        do_syscall(1, (u64) "spawn_builtin(re-registered) launched task_index 0x", reused_task_index, 0);
    } else if (trigger_value == 16) {
        // trigger 16 (ring3perms): real UID-based file ownership +
        // permission enforcement (kernel/fs/minifs/minifs.h's
        // MODE_OWNER_ONLY_READ, proc/ipc/file/file.c's real check).
        gt_setuid(1);  // become a non-root test user
        int owner_handle = gt_file_open("/system/permtest.mfs", 1);
        do_syscall(1, (u64) "(uid=1) create permtest.mfs handle=0x", (u64) owner_handle, 0);
        const char* secret = "owner-only content";
        gt_file_write(owner_handle, (const u8*) secret, 19);
        gt_file_close(owner_handle);  // real Unix "creator becomes owner" - now really owned by uid 1

        gt_fs_set_mode("permtest.mfs", MODE_OWNER_ONLY_READ);

        int owner_read = gt_file_open("/system/permtest.mfs", 0);
        do_syscall(1, (u64) "(uid=1, owner) read handle=0x", (u64) owner_read, 0);
        gt_file_close(owner_read);

        gt_setuid(5);  // an arbitrary unrelated non-root uid
        int stranger_read = gt_file_open("/system/permtest.mfs", 0);
        do_syscall(1, (u64) "(uid=5, non-owner, non-root) read handle=0x", (u64) stranger_read, 0);

        gt_setuid(0);  // root
        int root_read = gt_file_open("/system/permtest.mfs", 0);
        do_syscall(1, (u64) "(uid=0, root) read handle=0x", (u64) root_read, 0);
        gt_file_close(root_read);
    } else if (trigger_value == 29) {
        // trigger 29 (ring3users) - Faza I point 14 item 6: real user
        // accounts backing a uid, not just a bare number. Deliberately
        // does NOT touch gt_setuid/ring3perms - item 7 (permission-
        // gating) is where enforcement actually starts using this table;
        // this trigger only proves the lookup itself distinguishes a
        // real registered account from an arbitrary uid.
        char name0[32];
        u8 gid0;
        bool found0 = gt_user_lookup(0, &name0[0], &gid0);
        do_syscall(1, (u64) "uid=0x0 found=0x", (u64) found0, 0);
        if (found0) {
            do_syscall(1, (u64) "  name: ", 0, 0);
            do_syscall(1, (u64) &name0[0], 0, 0);
            do_syscall(1, (u64) "  gid=0x", (u64) gid0, 0);
        }

        char name100[32];
        u8 gid100;
        bool found100 = gt_user_lookup(100, &name100[0], &gid100);
        do_syscall(1, (u64) "uid=0x64 found=0x", (u64) found100, 0);
        if (found100) {
            do_syscall(1, (u64) "  name: ", 0, 0);
            do_syscall(1, (u64) &name100[0], 0, 0);
            do_syscall(1, (u64) "  gid=0x", (u64) gid100, 0);
        }

        char name99[32];
        u8 gid99;
        bool found99 = gt_user_lookup(99, &name99[0], &gid99);
        do_syscall(1, (u64) "uid=0x63 found=0x", (u64) found99, 0);
    } else if (trigger_value == 30) {
        // trigger 30 (ring3vfsperm) - Faza I point 5 item 7: proves the
        // OLDER raw vfs_read path (syscall 4, NOT the File-object path
        // ring3perms/trigger 16 already covers) is now really permission-
        // gated too. Seeds a real owner=1/MODE_OWNER_ONLY_READ file the
        // same way ring3perms does (file_object_open/write/close +
        // gt_fs_set_mode), then reads it via the RAW gt_vfs_read
        // directly - bypassing file_object_open's own separate pre-check
        // entirely - as a non-owner/non-root uid (must fail), then as
        // the real owner and as root (must both succeed).
        gt_setuid(1);
        int owner_handle = gt_file_open("/system/vfsperm.mfs", 1);
        const char* secret = "vfs-layer-owner-only";
        gt_file_write(owner_handle, (const u8*) secret, 20);
        gt_file_close(owner_handle);  // real Unix "creator becomes owner" - now owned by uid 1

        gt_fs_set_mode("vfsperm.mfs", MODE_OWNER_ONLY_READ);

        gt_setuid(5);  // an arbitrary unrelated non-root uid
        char buf5[32];
        int n5 = gt_vfs_read("/system/vfsperm.mfs", (u8*) &buf5[0], 31);
        do_syscall(1, (u64) "(uid=5, non-owner, non-root) raw vfs_read n=0x", (u64) n5, 0);

        gt_setuid(1);  // owner
        char buf1[32];
        int n1 = gt_vfs_read("/system/vfsperm.mfs", (u8*) &buf1[0], 31);
        do_syscall(1, (u64) "(uid=1, owner) raw vfs_read n=0x", (u64) n1, 0);

        gt_setuid(0);  // root
        char buf0[32];
        int n0 = gt_vfs_read("/system/vfsperm.mfs", (u8*) &buf0[0], 31);
        do_syscall(1, (u64) "(uid=0, root) raw vfs_read n=0x", (u64) n0, 0);
    } else if (trigger_value == 31) {
        // trigger 31 (ring3fork) - Faza I point 4 item 8: real copy-on-
        // write fork(). x=100 lives on this task's own COW-shareable
        // stack; each side's own assignment below is a real write to
        // that shared page, triggering the new page-fault repair path
        // independently for parent and child - if COW were broken, both
        // would see the SAME frame and either corrupt or overwrite each
        // other's value.
        u64 x = 100;
        do_syscall(1, (u64) "before fork, x=0x", x, 0);
        u64 fork_result = do_syscall(95, 0, 0, 0);
        if (fork_result == 0) {
            x = 300;
            do_syscall(1, (u64) "child: x=0x", x, 0);
            do_syscall(12, 0, 0, 0);  // process_exit - never returns
            for (;;) {
            }
        }
        x = 200;
        do_syscall(1, (u64) "parent: child_task=0x", fork_result, 0);
        do_syscall(1, (u64) "parent: x=0x", x, 0);
    } else if (trigger_value == 32) {
        // trigger 32 (ring3guard) - Faza I point 4 item 8: real guard
        // pages. KERNEL-HALTING, run standalone. This process's own
        // stack sits at a single mapped page (0x80020000, the same
        // fixed address ring3nx's own stack-execution test already
        // relies on) - clone_address_space()'s lazy, sparse PDPT[2]+
        // population never maps anything below it unless something
        // explicitly asked for that address, a real guard page by
        // construction. Deliberately underflows by 8 bytes - must fault
        // with the present bit CLEAR (error_code 0x6: user+write+not-
        // present), distinctly different from ring3fault's 0x7
        // (present, wrong permission) and ring3nx's 0x15 (present+NX).
        do_syscall(1, (u64) "attempting a deliberate stack-guard underflow write at 0x", 0x80020000 - 8, 0);
        u64* guard = (u64*) (0x80020000 - 8);
        *guard = 0xDEADBEEF;
        do_syscall(1, (u64) "guard write succeeded (BUG!)", 0, 0);
    } else if (trigger_value == 33) {
        // trigger 33 (ring3wait) - Faza I point 3 item 9: real high-level
        // Process.spawn()/.wait() API. gt_process_wait() (syscall 96)
        // must genuinely block until the target exits, not race ahead.
        // Reuses trigger 31's own real fork() (item 8) as the child-
        // creation vehicle - simpler than routing a fresh spawn_process()
        // child through the shared boot-listener channel, and just as
        // real a process boundary.
        u64 fork_result = do_syscall(95, 0, 0, 0);
        if (fork_result == 0) {
            int timer_handle = gt_timer_create(30);
            gt_timer_wait(timer_handle);
            do_syscall(1, (u64) "ring3wait child: done waiting, exiting", 0, 0);
            do_syscall(12, 0, 0, 0);  // process_exit - never returns
            for (;;) {
            }
        }
        int handle = gt_process_open((int) fork_result, RIGHT_QUERY);
        u64 start_tick = gt_get_ticks();
        bool waited = gt_process_wait(handle);
        u64 elapsed = gt_get_ticks() - start_tick;
        do_syscall(1, (u64) "ring3wait parent: wait ok=0x", (u64) waited, 0);
        do_syscall(1, (u64) "ring3wait parent: elapsed_ticks=0x", elapsed, 0);
        u64 post_query = gt_process_query(handle);
        do_syscall(1, (u64) "ring3wait parent: post-wait query=0x", post_query, 0);
    } else if (trigger_value == 35) {
        // trigger 35 (ring3signfail) - Faza I point 14, item 17: the
        // real, concrete NEGATIVE assertion the signature check needs -
        // not just "a signed program runs" (already proven by the
        // ordinary install-then-spawn shell demo), but that an UNSIGNED
        // blob planted directly on writable storage is cleanly refused,
        // not crashed on. Deliberately plain, unrelated bytes - no magic,
        // no header, nothing that could coincidentally look signed.
        const char* path = "/system/unsigned_test.bin";
        const char* garbage = "this is not a signed executable, just plain bytes";
        u64 write_ok = do_syscall(5, (u64) path, (u64) garbage, 51);
        do_syscall(1, (u64) "ring3signfail: raw write ok=0x", write_ok, 0);
        u64 spawn_result = do_syscall(6, (u64) path, 0x80000000, 0x80020000);
        do_syscall(1, (u64) "ring3signfail: spawn of unsigned blob result=0x", spawn_result, 0);
    } else {
        return false;
    }
    return true;
}
