// Milestone 36: a trivial "service" - the thing proc/init.c spawns to
// prove real userspace-driven process orchestration works. Deliberately
// as small as possible: this file's only real content is the fact that
// a NEW ring3 process running it exists at all, spawned by another
// ring3 process (init) rather than by kmain.c directly - the genuinely
// new capability that milestone adds.
//
// Milestone 37: exits for real right after its own startup message,
// via the new process_exit() syscall, instead of idling forever like
// every other ring3 process in this kernel so far - the cleanest
// possible checkable claim for "a process can genuinely stop running":
// `ps` shows this process's `exited=0x1` immediately, while every other
// process (the boot-time demo, init) still shows `exited=0x0`.

#include "../types.h"

#define SYS_PRINT 1
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
    do_syscall(SYS_PRINT, (u64) "hello_service: running, spawned by init 0x", 1, 0);
    do_syscall(SYS_PROCESS_EXIT, 0, 0, 0);

    // Never actually reached - process_exit() never returns. A plain
    // spin rather than nothing at all, purely so a compiler that ever
    // stopped trusting that (e.g. a future change to this file) fails
    // loud with a real crash instead of falling off the end of _start
    // into whatever bytes happen to follow it in the loaded image.
    for (;;) {
    }
}
