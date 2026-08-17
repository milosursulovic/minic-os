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
// num 6 (milestone 23): spawn - wraps Process the same way num 4/5
// wrapped File. Reuses spawnProcessFromPath() completely unchanged (the
// exact same function the shell's `spawn` command already calls in
// kernel mode - only the caller is new, not the mechanism) and returns
// the new process's taskIndex, same convention syscall 3 already
// established.
// num 7/8/9 (milestone 23, reworked in milestone 25): channelSend/
// channelReceive/openChannel. channelReceive is the first BLOCKING
// syscall this kernel has ever had - channelReceive() calls
// yield()/switch_context() same as it always has when called from a
// kernel task directly (procReceiverEntry), and since syscall_dispatch
// runs as an ordinary nested call within the *calling* ring3 task's own
// context, blocking here suspends the right task and correctly resumes
// through isr_syscall's iretq once woken.
//
// Milestone 25: num 7/8's arg1 used to be a raw channel INDEX, bounds-
// checked against gChannelCount but with no ownership concept at all -
// any ring3 process that could guess a valid index (0-3, and every
// index this kernel actually uses is baked into ring3prog.mc's own
// source as a fixed constant, so "guess" barely undersells it) could
// send/receive on ANY channel, not just one it was actually given. Now
// arg1 is a real HANDLE, resolved through the calling process's own
// handle table with real per-handle RIGHTS checked before the
// underlying channel is touched at all - the actual "capability" in
// "capability/permission system" (roadmap phase IX). num 9 (openChannel)
// is the one place a ring3 process can turn a channel index into a
// handle - and it's also the one place rights POLICY is decided: it
// always grants RIGHT_RECEIVE only, never RIGHT_SEND, regardless of
// what the caller might want, since nothing in this kernel today needs
// a ring3-initiated send (the shell/kernel side always sends directly).
// A handle's rights are fixed forever at grant time (see object.mc's
// allocHandle) - there's no way to widen one later, only to open a new
// one under whatever policy the kernel chooses at that call site.
//
// Milestone 27: num 3 (query) used to resolve a handle via resolveHandle()
// and return ground truth with NO rights check at all - object.mc's own
// RIGHT_QUERY comment even named this exact syscall as the right's
// purpose, but nothing ever verified it. A real, if narrow, enforcement
// gap: any valid OBJ_PROCESS handle could be queried regardless of its
// rights bitmask. Fixed by inlining the same handle-table/rights-check
// style syscalls 7/8/9 already use, replacing the old resolveHandle()
// call. New num 10 (openProcess) is the first real cross-process
// capability: given another task's index (not necessarily the caller's
// own), it mints a handle to THAT process in the caller's own table -
// the caller REQUESTS a rights bitmask (arg2), and the kernel grants the
// intersection of what was requested and what's actually grantable for
// an OBJ_PROCESS handle today (`requested & RIGHT_QUERY` - the only real
// Process operation that exists). Requesting 0 (or any bits outside
// RIGHT_QUERY) yields a handle that's real and valid but can do nothing
// - the actual negative-space proof this milestone needed, using a real
// caller-controllable mechanism rather than a testing-only backdoor.
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
        int handle = (int) arg1;
        if (handle < 0 || handle >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!gHandleTables[callerProcess][handle].used) {
            return (u64) -1;
        }
        if ((gHandleTables[callerProcess][handle].rights & RIGHT_QUERY) == 0) {
            return (u64) -1;
        }
        int objIndex = gHandleTables[callerProcess][handle].objectIndex;
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
    if (num == 6) {
        char* path = (char*) arg1;
        int procIndex = spawnProcessFromPath(path, arg2, arg3);
        if (procIndex < 0) {
            return (u64) -1;
        }
        return (u64) gProcesses[procIndex].taskIndex;
    }
    if (num == 7) {
        int callerProcess = gTasks[gCurrentTask].processIndex;
        if (callerProcess < 0) {
            return (u64) -1;
        }
        int handle = (int) arg1;
        if (handle < 0 || handle >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!gHandleTables[callerProcess][handle].used) {
            return (u64) -1;
        }
        if ((gHandleTables[callerProcess][handle].rights & RIGHT_SEND) == 0) {
            return (u64) -1;
        }
        int objIndex = gHandleTables[callerProcess][handle].objectIndex;
        if (gObjects[objIndex].type != OBJ_CHANNEL) {
            return (u64) -1;
        }
        int channelIndex = gObjects[objIndex].dataIndex;
        bool ok = channelSend(channelIndex, arg2);
        if (!ok) {
            return (u64) -1;
        }
        return 0;
    }
    if (num == 8) {
        int callerProcess = gTasks[gCurrentTask].processIndex;
        if (callerProcess < 0) {
            return (u64) -1;
        }
        int handle = (int) arg1;
        if (handle < 0 || handle >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!gHandleTables[callerProcess][handle].used) {
            return (u64) -1;
        }
        if ((gHandleTables[callerProcess][handle].rights & RIGHT_RECEIVE) == 0) {
            return (u64) -1;
        }
        int objIndex = gHandleTables[callerProcess][handle].objectIndex;
        if (gObjects[objIndex].type != OBJ_CHANNEL) {
            return (u64) -1;
        }
        int channelIndex = gObjects[objIndex].dataIndex;
        return channelReceive(channelIndex);
    }
    if (num == 9) {
        int callerProcess = gTasks[gCurrentTask].processIndex;
        if (callerProcess < 0) {
            return (u64) -1;
        }
        int channelIndex = (int) arg1;
        if (channelIndex < 0 || channelIndex >= gChannelCount) {
            return (u64) -1;
        }
        int objIndex = allocObject(OBJ_CHANNEL, channelIndex);
        if (objIndex < 0) {
            return (u64) -1;
        }
        int handle = allocHandle(callerProcess, objIndex, RIGHT_RECEIVE);
        if (handle < 0) {
            return (u64) -1;
        }
        return (u64) handle;
    }
    if (num == 10) {
        int callerProcess = gTasks[gCurrentTask].processIndex;
        if (callerProcess < 0) {
            return (u64) -1;
        }
        int targetTask = (int) arg1;
        if (targetTask < 0 || targetTask >= gTaskCount) {
            return (u64) -1;
        }
        int targetProcess = gTasks[targetTask].processIndex;
        if (targetProcess < 0) {
            return (u64) -1;
        }
        int objIndex = allocObject(OBJ_PROCESS, targetProcess);
        if (objIndex < 0) {
            return (u64) -1;
        }
        int grantedRights = ((int) arg2) & RIGHT_QUERY;
        int handle = allocHandle(callerProcess, objIndex, grantedRights);
        if (handle < 0) {
            return (u64) -1;
        }
        return (u64) handle;
    }
    return (u64) -1;   // unknown syscall
}
