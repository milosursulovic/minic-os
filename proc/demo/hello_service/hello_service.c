// Trivial service spawned by init.c - exits immediately via
// process_exit() (syscall 12), so ps shows it exited=0x1.

#include "../../../types.h"

#define SYS_PROCESS_EXIT 12

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
    do_syscall(SYS_PROCESS_EXIT, 0, 0, 0);

    for (;;) {  // unreachable - process_exit() never returns
    }
}
