// Milestone 36 (Phase XI's first step): the kernel's first real init
// process - a genuine, dedicated ring3 program (compiled standalone and
// flattened into a blob, same pipeline as proc/ring3prog.c) whose only
// job is real service orchestration, spawned by kmain.c at boot the
// same way the existing boot-time demo process always has been.
//
// Deliberately narrow scope: proves a ring3 process can itself spawn
// ANOTHER ring3 process from a kernel-embedded program (see syscall.c's
// new syscall 11, spawn_builtin) without touching the filesystem at all
// - a fresh disk.img has no files on it until `mkfs`+`install` run
// manually, so a real "userspace controls what runs next" story can't
// depend on the VFS existing yet. Does NOT touch or replace the
// existing boot-time demo process (proc/ring3prog.c, which still
// exercises the full File/Channel/Process/POSIX-shim/rights surface
// exactly as before) or the kernel's own debug shell (shell/shell.c,
// which stays kernel-mode - real per-syscall exposure for the ~40
// existing debug commands, most of which touch raw kernel internals
// like physical frames directly, is a substantially bigger, separate
// problem than "does a real init process exist at all").

#include "../types.h"

#define SYS_PRINT 1
#define SYS_SPAWN_BUILTIN 11

#define BUILTIN_HELLO_SERVICE 0

static u64 do_syscall(u64 num, u64 arg1, u64 arg2, u64 arg3) {
    u64 result;
    register u64 r_num __asm__("rax") = num;
    register u64 r_arg1 __asm__("rdi") = arg1;
    register u64 r_arg2 __asm__("rsi") = arg2;
    register u64 r_arg3 __asm__("rdx") = arg3;
    __asm__ volatile("int $0x80"
                      : "+r"(r_num)
                      : "r"(r_arg1), "r"(r_arg2), "r"(r_arg3)
                      : "memory");
    result = r_num;
    return result;
}

__attribute__((section(".text.start")))
void _start(void) {
    do_syscall(SYS_PRINT, (u64) "init: starting 0x", 1, 0);

    u64 child_task_index = do_syscall(SYS_SPAWN_BUILTIN, BUILTIN_HELLO_SERVICE, 0, 0);

    do_syscall(SYS_PRINT, (u64) "init: spawned hello_service, task_index=0x", child_task_index, 0);

    for (;;) {
    }
}
