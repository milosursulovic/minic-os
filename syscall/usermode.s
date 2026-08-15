# Milestone 11's ring3 entry: builds a fake `iretq` frame (same trick
# interrupts.s already relies on for returning FROM a ring3 interrupt,
# just used here to enter ring3 for the first time instead) and jumps
# into it. Below what asm("...") can express for the usual reason - a
# privilege-level transition is program structure, not a value a MiniC
# statement could carry.
#
# void run_ring3_test(u64 entry, u64 userStack)
#   rdi = ring3 entry point
#   rsi = ring3 stack pointer (top of a page already mapped present +
#         writable + *user*, or this faults immediately on the first push)
#
# Never returns the normal way - there's no ring3 "exit" yet (milestone
# 12+'s job, once there's a real process model to exit *to*). Instead
# this saves its own suspend point into gKernelTestRsp, the same way
# sched/task.mc's switch_context() saves a task's rsp - and
# syscall.mc's syscall_dispatch(), for its one "exit" syscall number,
# loads that back and pops+rets directly, faking a normal return from
# this function without ever coming back through isr_syscall's iretq.

.intel_syntax noprefix
.code64

.extern gKernelTestRsp
.global run_ring3_test

run_ring3_test:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rip + gKernelTestRsp], rsp

    mov ax, 0x23        # user data selector (GDT index 4 | RPL 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    # ss isn't reloaded here - iretq below sets it from the pushed frame

    push 0x23            # SS
    push rsi              # RSP
    pushfq                 # RFLAGS (IF is already 1 - we're long past boot's `sti`)
    push 0x1B             # CS (GDT index 3 | RPL 3)
    push rdi               # RIP (entry point)
    iretq
