// The kernel's first real init process - spawns hello_service.c via
// spawn_builtin (syscall 11), avoiding the filesystem entirely.

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
