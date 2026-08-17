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
import "../sched/task.mc";
import "../proc/process.mc";
import "../proc/object.mc";
import "../disk/vfs.mc";

extern void run_ring3_test(u64 entry, u64 userStack);

// num 1: print arg1 (a char* the caller owns) followed by arg2 in hex.
// milestone 11's ring3 demo used the hex slot for its own CS register
// value, proving CPL 3 directly; milestone 13's loaded process (proc/
// testprog.s) uses it for an arbitrary marker constant instead.
//
// There used to be a num 2 ("exit the one-shot ring3 test") here -
// milestone 11's ring3TestEntry()/runRing3Test() demo, since retired.
// It worked by popping a saved kernel rsp and `ret`-ing directly instead
// of going back through isr_syscall's iretq - which relied on it being
// the ONLY thing ever using the shared TSS.RSP0 stack for a ring3->ring0
// transition. Milestone 13 gave real per-task ring3 processes (proc/
// process.mc) that can be legitimately suspended mid-ring3 (preempted,
// waiting for the round-robin to cycle back to them) with their own
// state still resident on that same shared stack - triggering the old
// demo while a real process was in exactly that state reset RSP0 out
// from under it, corrupting the process's saved state and hanging the
// kernel the next time it was scheduled. Same bug *class* as milestone
// 11's original RSP0 discovery (an absolute reset point colliding with
// something still in use), just recurring now that two independent
// ring3-entry paths existed at once. Fixed by retiring the one-shot
// demo entirely, rather than trying to make two incompatible mechanisms
// coexist - milestone 13's real mechanism proves everything it did
// (ring3 entry + syscalls + CPL) and more, so nothing is lost.
// num 3: resolve arg1 as a handle *within the calling process's own
// handle table* (milestone 14) and return a piece of ground-truth info
// about whatever it points to - for a process object, its taskIndex,
// independently checkable against the `ps` shell command's own output.
// A handle that's out of range, never allocated, or belongs to a plain
// kernel task with no handle table at all (gTasks[...].processIndex < 0
// - can't happen for real ring3 code today, since only spawnProcess()
// tasks ever reach syscall_dispatch from ring3, but checked anyway since
// arg1 is caller-controlled) all return the same -1 sentinel rather than
// trusting the index or crashing - the entire point of a handle table
// over a raw array index.
// num 4/5 (milestone 22): vfsRead/vfsWrite, the first syscalls giving
// ring3 code access to something beyond print/handle-query - a real
// slice of the native "File" API's job, not the whole thing (Process/
// Channel wrappers are a later milestone). arg1/arg2 are a caller-owned
// char*/buffer pointer pair; since a syscall runs with the caller's own
// CR3 still loaded (no address-space switch happens on entry), these
// are safe to dereference directly, same as syscall 1's message pointer
// always has been - no new mechanism needed for that part. Neither
// direction validates that arg1/arg2 point at memory the calling
// process actually owns (the same "no real memory-safety enforcement
// yet" gap every ring3 syscall here has had since milestone 11 -
// capability/security work is roadmap phase IX, not this one).
u64 syscall_dispatch(u64 num, u64 arg1, u64 arg2, u64 arg3) {
    if (num == 1) {
        char* s = (char*) arg1;
        serialPrint(s);
        vgaPrint(s);
        printHex(arg2);
        serialPrint("\n");
        return 0;
    }
    if (num == 3) {
        int callerProcess = gTasks[gCurrentTask].processIndex;
        if (callerProcess < 0) {
            return (u64) -1;
        }
        int objIndex = resolveHandle(callerProcess, (int) arg1);
        if (objIndex < 0) {
            return (u64) -1;
        }
        if (gObjects[objIndex].type == OBJ_PROCESS) {
            int procIdx = gObjects[objIndex].dataIndex;
            return (u64) gProcesses[procIdx].taskIndex;
        }
        return (u64) -1;
    }
    if (num == 4) {
        char* path = (char*) arg1;
        u8* buf = (u8*) arg2;
        int n = vfsRead(path, buf, (u32) arg3);
        if (n < 0) {
            return (u64) -1;
        }
        return (u64) n;
    }
    if (num == 5) {
        char* path = (char*) arg1;
        u8* buf = (u8*) arg2;
        bool ok = vfsWrite(path, buf, (u32) arg3);
        if (!ok) {
            return (u64) -1;
        }
        return arg3;
    }
    return (u64) -1;   // unknown syscall
}
