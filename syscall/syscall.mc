// Milestone 11: the first real ring0/ring3 boundary. `int 0x80` (DPL=3,
// wired up in drivers/interrupts_init.mc) is the gate; boot/interrupts.s's
// isr_syscall stub reads the caller's syscall number/args off the stack
// and calls syscall_dispatch() below - same relationship isr.mc's
// interrupt_handler has to the hardware interrupt stubs, just with real
// arguments and a real return value instead of just a vector number.
//
// Calling convention (ours to define, since this goes through a software
// `int n` gate rather than the SYSCALL/SYSRET instruction pair with its
// own fixed convention): rax = syscall number in, return value out;
// rdi/rsi/rdx = up to three arguments.

import "../drivers/io.mc";
import "../lib/strings.mc";
import "../mm/frames.mc";
import "../mm/paging.mc";

extern void run_ring3_test(u64 entry, u64 userStack);

u64 gKernelTestRsp;

u64 gSyscallNum;
u64 gSyscallArg1;
u64 gSyscallArg2;
u64 gSyscallArg3;
u64 gSyscallResult;

// Relays through globals and a raw asm block, same reason outb/inb do -
// `int n` (like `out`/`in`) needs specific values in specific registers
// at the moment it executes, and asm("...") has no operand binding to
// arrange that directly from MiniC expressions.
u64 doSyscall(u64 num, u64 arg1, u64 arg2, u64 arg3) {
    gSyscallNum = num;
    gSyscallArg1 = arg1;
    gSyscallArg2 = arg2;
    gSyscallArg3 = arg3;
    asm("mov rax, [rip+gSyscallNum]\nmov rdi, [rip+gSyscallArg1]\nmov rsi, [rip+gSyscallArg2]\nmov rdx, [rip+gSyscallArg3]\nint 0x80\nmov [rip+gSyscallResult], rax");
    return gSyscallResult;
}

// num 1: print arg1 (a char* the caller owns) followed by arg2 in hex -
// the ring3 test uses the hex slot for its own CS register value, so the
// serial log ends up with direct, checkable proof of which ring it
// actually ran in (0x1B & 3 == 3, and 0x1B >> 3 == 3 - GDT index 3, the
// ring3 code segment boot.s set up), not just "a syscall happened."
//
// num 2: exit the one-shot ring3 test - see usermode.s's comment for how
// this is fundamentally different from a normal return: it never goes
// back through isr_syscall's iretq at all, so nothing after this branch
// in the function - including the caller's own asm("iretq")-adjacent
// bookkeeping - ever runs for this call.
u64 syscall_dispatch(u64 num, u64 arg1, u64 arg2, u64 arg3) {
    if (num == 1) {
        char* s = (char*) arg1;
        serialPrint(s);
        vgaPrint(s);
        printHex(arg2);
        serialPrint("\n");
        return 0;
    }
    if (num == 2) {
        asm("mov rsp, [rip+gKernelTestRsp]\npop r15\npop r14\npop r13\npop r12\npop rbx\npop rbp\nret");
    }
    return (u64) -1;   // unknown syscall
}

u16 gRing3Cs;

void ring3TestEntry() {
    asm("mov [rip+gRing3Cs], cs");
    doSyscall(1, (u64) "ring3 alive, cs=0x", (u64) gRing3Cs, 0);
    doSyscall(1, (u64) "ring3 alive again, cs=0x", (u64) gRing3Cs, 0);
    doSyscall(2, 0, 0, 0);
    while (true) { }   // unreachable - syscall 2 never returns here
}

bool runRing3Test() {
    void* stackFrame = allocFrame();
    if (stackFrame == null) {
        return false;
    }
    u64 userStackVaddr = 0x60000000;   // clear of the map demo (0x40000000) and the heap (0x50000000+)
    if (!mapPage(userStackVaddr, (u64) stackFrame, 0x06)) {   // writable + user
        freeFrame(stackFrame);
        return false;
    }
    u64 userStackTop = userStackVaddr + 4096;

    // Nothing here is scheduler-aware yet - the ring3 test isn't a
    // sched/task.mc task, so a timer tick landing mid-test would call
    // yield() against whatever kernel task happened to be "current" when
    // this was invoked, corrupting its saved state (see the milestone 11
    // writeup). Kept simple on purpose: cli around the one-shot test,
    // sti after it returns. Real preemption-safe ring3 tasks are
    // milestone 12+'s job, once there's a real per-task register/CR3
    // frame to save instead of borrowing whichever kernel task's slot
    // happens to be active.
    asm("cli");
    run_ring3_test((u64) &ring3TestEntry, userStackTop);
    asm("sti");
    return true;
}
