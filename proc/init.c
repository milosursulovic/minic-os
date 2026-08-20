// The kernel's first real init process - spawns hello_service.c via
// spawn_builtin (syscall 11), then supervises it: polls its handle
// until the exit-aware query (syscall 3) reports it gone, closes that
// handle (syscall 13), and restarts it. Loops 3 times - safe now that
// handle_close frees the query handle's object each round instead of
// leaking it (the exact aggressive-restart scenario that exhausted
// g_objects[8] during milestone 40's own testing, before reclaim
// existed at all). No yield/sleep syscall exists for ring3 - polling
// busy-spins.

#include "../types.h"

#define SYS_PRINT 1
#define SYS_QUERY 3
#define SYS_SPAWN_BUILTIN 11
#define SYS_OPEN_PROCESS 10
#define SYS_HANDLE_CLOSE 13

#define RIGHT_QUERY 1
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

    u64 task_index = do_syscall(SYS_SPAWN_BUILTIN, BUILTIN_HELLO_SERVICE, 0, 0);
    do_syscall(SYS_PRINT, (u64) "init: spawned hello_service, task_index=0x", task_index, 0);

    int round = 0;
    while (round < 3) {
        u64 handle = do_syscall(SYS_OPEN_PROCESS, task_index, RIGHT_QUERY, 0);
        u64 status;
        do {
            status = do_syscall(SYS_QUERY, handle, 0, 0);
        } while (status != (u64) -1);
        do_syscall(SYS_HANDLE_CLOSE, handle, 0, 0);
        do_syscall(SYS_PRINT, (u64) "init: hello_service exited, restarting 0x", 1, 0);

        task_index = do_syscall(SYS_SPAWN_BUILTIN, BUILTIN_HELLO_SERVICE, 0, 0);
        do_syscall(SYS_PRINT, (u64) "init: restarted hello_service, task_index=0x", task_index, 0);
        round = round + 1;
    }

    for (;;) {
    }
}
