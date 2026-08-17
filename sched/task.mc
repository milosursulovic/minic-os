// A fixed table of kernel tasks, each with its own stack, round-robin
// scheduled. Switching happens both voluntarily (a task calls yield())
// and preemptively (the timer interrupt calls it too - see isr.mc and
// switch.s's comments for how that works on top of the same mechanism).
//
// Task 0 is special: it's not created via createTask() - it's whatever
// context called schedulerInit() (kmain.mc's _start), captured the first
// time IT calls yield(). Every other task's initial stack is hand-built
// in createTask() to look exactly like what switch_context() would have
// left behind, so the very first switch into it lands on `entry` via an
// ordinary `ret` - a fake "return address" baked into a fresh stack.
//
// sleep() adds real blocking on top of that: a task can take itself out
// of the round-robin entirely until a given tick, instead of the only
// two options being "spin forever" (task3Entry) or "yield every
// iteration and get scheduled again immediately" (task1Entry/
// task2Entry). yield() skips blocked tasks when picking who runs next,
// waking one up (clearing `blocked`) the moment its wake tick arrives.

import "../mm/heap.mc";
import "../mm/frames.mc";
import "../mm/paging.mc";
import "../isr/isr.mc";
import "../proc/channel.mc";

extern void switch_context(u64* oldRspOut, u64 newRsp);

struct Task {
    u64 savedRsp;
    bool used;
    bool blocked;
    u64 wakeTick;   // only meaningful while blocked
    u64 cr3;        // this task's address space (milestone 12)
    u64 ring3EntryVaddr;    // 0 = plain kernel task; else jump here via run_ring3_test (milestone 13)
    u64 ring3UserStackTop;  // only meaningful when ring3EntryVaddr != 0
    int processIndex;       // -1 = plain kernel task; else an index into gProcesses[] (milestone 14)
    int waitingChannel;     // -1 = blocked on a tick (wakeTick), else blocked on gChannels[this] (milestone 15)
    u64 kernelStackTop;     // this task's own TSS.RSP0 target (milestone 19) - see setTssRsp0's comment
}

// Sized with headroom past what boots today (task 0 + task1-4 + procA/
// procB + the spawned ring3 process + the channel demo's sender/
// receiver = 10) - task structs are cheap (no heap cost, each task's
// real 16KB stack is a separate kalloc()), so oversizing this array
// costs nothing but a few dozen bytes of zeroed .bss.
Task gTasks[16];
int gTaskCount;
int gCurrentTask;

void schedulerInit() {
    gTasks[0].used = true;
    gTasks[0].cr3 = gPML4Phys;   // the shell/main loop keeps using the boot-time kernel space
    gTaskCount = 1;
    gCurrentTask = 0;
}

bool createTaskWithCr3(fn() -> void entry, u64 cr3) {
    if (gTaskCount >= 16) {
        return false;
    }
    u8* stackMem = (u8*) kalloc(16384);
    if (stackMem == null) {
        return false;
    }
    u64 stackTop = ((u64) stackMem + 16384) & ~((u64) 15);

    u64* sp = (u64*) stackTop;
    sp = sp - 1; *sp = (u64) entry;   // fake return address - switch_context's `ret` lands here
    sp = sp - 1; *sp = 0;   // rbp
    sp = sp - 1; *sp = 0;   // rbx
    sp = sp - 1; *sp = 0;   // r12
    sp = sp - 1; *sp = 0;   // r13
    sp = sp - 1; *sp = 0;   // r14
    sp = sp - 1; *sp = 0;   // r15

    Task* t = &gTasks[gTaskCount];
    t->savedRsp = (u64) sp;
    t->used = true;
    t->cr3 = cr3;
    t->processIndex = -1;   // plain kernel task by default - 0 would wrongly look like gProcesses[0]
    t->waitingChannel = -1;   // not waiting on a channel by default - 0 would wrongly look like gChannels[0]
    // This task's own kalloc'd stack, reused as its private TSS.RSP0
    // target (milestone 19). Safe even for non-ring3 tasks (yield()
    // only ever loads it into RSP0 for a ring3-capable task) and safe
    // for ring3 tasks specifically because once processEntryTrampoline()
    // -> run_ring3_test() iretqs into ring3, this stack is never touched
    // by ordinary execution again (run_ring3_test never returns) - so
    // there's nothing left on it worth preserving the moment a ring3->
    // ring0 transition needs a fresh reset point.
    t->kernelStackTop = stackTop;
    gTaskCount = gTaskCount + 1;
    return true;
}

// Every task before milestone 12 (task1-4) shares the kernel's own
// address space - identical behavior to before this milestone existed.
bool createTask(fn() -> void entry) {
    return createTaskWithCr3(entry, gPML4Phys);
}

// A task with its own private address space (see paging.mc's
// cloneAddressSpace()) - kernel code/heap stay reachable exactly as
// before, but anything this task maps at or past 0x80000000 is genuinely
// private to it.
bool createIsolatedTask(fn() -> void entry) {
    u64 cr3 = cloneAddressSpace();
    if (cr3 == 0) {
        return false;
    }
    return createTaskWithCr3(entry, cr3);
}

void yield() {
    int prev = gCurrentTask;
    int next = prev;
    int scanned = 0;
    // Walk the ring looking for a runnable task, waking up anything
    // whose wake condition is satisfied as we pass it - task 0 (the
    // shell/main loop) never blocks itself, so this is guaranteed to
    // terminate with *something* runnable (at worst, right back at
    // `prev`) rather than looping forever with nothing to switch to.
    //
    // Two different wake conditions share this one scan (milestone 15):
    // a plain sleep() waits for a tick, a channelReceive() waits for a
    // message - waitingChannel is how a blocked task says which one it
    // means. This is the exact generalization the roadmap called for:
    // reusing sleep()'s blocking mechanism for IPC, not inventing a
    // second one next to it.
    while (scanned < gTaskCount) {
        next = (next + 1) % gTaskCount;
        scanned = scanned + 1;
        Task* candidate = &gTasks[next];
        if (candidate->blocked) {
            bool wake = false;
            if (candidate->waitingChannel >= 0) {
                wake = channelHasMessage(candidate->waitingChannel);
            } else {
                wake = gTickCount >= candidate->wakeTick;
            }
            if (wake) {
                candidate->blocked = false;
                candidate->waitingChannel = -1;
            }
        }
        if (!candidate->blocked) {
            break;
        }
    }
    if (next == prev || gTasks[next].blocked) {
        return;   // nothing else runnable right now - keep running prev
    }
    gCurrentTask = next;
    Task* prevTask = &gTasks[prev];
    Task* nextTask = &gTasks[next];
    // Loading CR3 here, before the stack switch, is safe even though
    // we're still running on prevTask's stack for one more instruction:
    // cloneAddressSpace() guarantees every task's PDPT[0]/PDPT[1] (all
    // kernel code, the current stack, the heap) resolve identically
    // across every address space - only the private per-process region
    // (vaddr >= 0x80000000) can differ, and nothing here touches that.
    loadCr3(nextTask->cr3);
    // Milestone 19: give the incoming task its own TSS.RSP0 if it's
    // ring3-capable - see setTssRsp0's comment (mm/paging.mc) for why a
    // single shared RSP0 broke the moment more than one ring3 process
    // could exist at once. Harmless to skip for a plain kernel task:
    // RSP0 is only ever consulted on a ring3->ring0 transition, which
    // can't happen for a task that's never in ring3 to begin with, so
    // leaving whatever the last ring3 task's RSP0 was in place costs
    // nothing.
    if (nextTask->ring3EntryVaddr != 0) {
        setTssRsp0(nextTask->kernelStackTop);
    }
    switch_context(&prevTask->savedRsp, nextTask->savedRsp);
    // Execution resumes here once some other task switches back to this
    // one. Interrupts are already back on by this point - see switch.s's
    // comment for why the `sti` lives there and not here.
}

// Takes the calling task out of the round-robin until at least `ticks`
// timer ticks from now. Must be called from the task's own context (it
// blocks gCurrentTask, then immediately yields away from itself).
void sleep(u64 ticks) {
    Task* self = &gTasks[gCurrentTask];
    self->blocked = true;
    self->wakeTick = gTickCount + ticks;
    yield();
}

// Blocks the calling task until a message arrives on channelIndex -
// marks itself blocked with waitingChannel set (instead of sleep()'s
// wakeTick) and yields away, exactly the shape sleep() already has just
// above. Never returns until a message is actually available; the
// `while` (not `if`) is defensive rather than load-bearing on this
// single-core scheduler - yield()'s scan only ever clears `blocked`
// once channelHasMessage() is already true, so by the time control
// returns here the condition has always already been met.
u64 channelReceive(int channelIndex) {
    while (!channelHasMessage(channelIndex)) {
        Task* self = &gTasks[gCurrentTask];
        self->blocked = true;
        self->waitingChannel = channelIndex;
        yield();
    }
    u64 value = gChannels[channelIndex].message;
    gChannels[channelIndex].full = false;
    return value;
}

// ---- Demo tasks - just enough to prove real switching is happening,
// not full workloads. task1/task2 are cooperative (voluntary yield every
// iteration, same as milestone 8); task3 deliberately never calls
// yield() at all - a tight busy loop. If preemption is really working,
// task3's counter should still climb (in big jumps - it spins for its
// *whole* timer slice each turn, unlike task1/task2's one-increment
// turns) *and* task1/task2/the shell should keep advancing too, proving
// the timer forces control away from it. If preemption were broken,
// task3 would hog the CPU forever and everything else - including the
// shell - would hang the instant its turn came up.

// task4 is the blocking demo: instead of spinning or yielding every
// iteration, it sleeps for 50 ticks (~500ms at the PIT's 100Hz) between
// increments, so its counter should climb at roughly gTickCount/50 -
// dramatically slower than task1/task2 (once per full ring rotation,
// effectively every timer tick) and worlds slower than task3 (millions
// per turn). If sleep()/the blocked-skipping logic in yield() were
// broken, task4 would either never advance at all (stuck blocked
// forever) or advance at the same rate as task1/task2 (blocking a no-op,
// same as an immediate yield()) - neither of which would track
// gTickCount/50.

u64 gTask1Ticks;
u64 gTask2Ticks;
u64 gTask3Ticks;
u64 gTask4Ticks;

void task1Entry() {
    while (true) {
        gTask1Ticks = gTask1Ticks + 1;
        yield();
    }
}

void task2Entry() {
    while (true) {
        gTask2Ticks = gTask2Ticks + 1;
        yield();
    }
}

void task3Entry() {
    while (true) {
        gTask3Ticks = gTask3Ticks + 1;
    }
}

void task4Entry() {
    while (true) {
        gTask4Ticks = gTask4Ticks + 1;
        sleep(50);
    }
}

// ---- Milestone 12 demo: two isolated "processes" both map the SAME
// virtual address (well past anything else this kernel uses) in their
// OWN private address space, each to a physical frame nobody else knows
// about, and write a distinct, recognizable constant there. If CR3
// switching + per-process PDPT[2..] isolation genuinely works, each
// keeps reading back its own value forever and their two demo pages
// resolve to different physical addresses. If it were broken - CR3 never
// actually switching, or the two processes secretly sharing one address
// space - both would read back whichever value was written most
// recently, and translateIn() would show the SAME physical address for
// both, since there'd only really be one page table entry for that vaddr
// in the whole system either way.

u64 gDemoVaddr = 0x80000000;   // 2GB - PDPT index 2, untouched by any earlier milestone
u32 gProcAValue;
u32 gProcBValue;
u64 gProcAPhys;
u64 gProcBPhys;

void procAEntry() {
    void* frame = allocFrame();
    mapPageIn(gTasks[gCurrentTask].cr3, gDemoVaddr, (u64) frame, 0x06 | PAGE_NX);   // writable + user, non-executable (milestone 28)
    u32* p = (u32*) gDemoVaddr;
    *p = 0xAAAAAAAA;
    while (true) {
        gProcAValue = *p;
        gProcAPhys = translateIn(gTasks[gCurrentTask].cr3, gDemoVaddr);
        yield();
    }
}

void procBEntry() {
    void* frame = allocFrame();
    mapPageIn(gTasks[gCurrentTask].cr3, gDemoVaddr, (u64) frame, 0x06 | PAGE_NX);   // writable + user, non-executable (milestone 28)
    u32* p = (u32*) gDemoVaddr;
    *p = 0xBBBBBBBB;
    while (true) {
        gProcBValue = *p;
        gProcBPhys = translateIn(gTasks[gCurrentTask].cr3, gDemoVaddr);
        yield();
    }
}

// ---- Milestone 15 demo: an isolated process blocked on channelReceive()
// since boot, with the shell itself as the sender (the `send` command,
// shell.mc) rather than a second task on a timer - deliberately, so the
// "still blocked" and "just woke up" states are operator-triggered and
// exactly reproducible instead of racing QEMU/TCG's timer (observed to
// run at wildly varying effective rates across sessions - the same
// gotcha the kernel-qemu-test skill already documents for gTickCount
// comparisons, now hit for a fixed sleep() duration too, not just a
// ticks-elapsed reading). procReceiverEntry calls channelReceive()
// immediately at boot, long before any `send` is ever typed - it
// genuinely blocks (every yield() scan skips it while the channel stays
// empty), for as long as an operator chooses to run other commands
// first, not just briefly. gReceiverGotMessage/gReceiverValue are only
// ever written after channelReceive() actually returns, so `chan`
// reading false across an arbitrary number of other commands, then true
// with the exact sent value the instant `send` runs, is the whole
// proof: genuinely still blocked (not a no-op, not a short timeout),
// and the message really crossed from the shell's own address space
// into this isolated process's (a separate cr3, same as procA/procB)
// through the channel, not shared memory.

int gChannelDemo;
bool gReceiverGotMessage;
u64 gReceiverValue;

// Milestone 23: a second, dedicated channel index for the ring3
// program's own Channel.receive() - see kmain.mc's comment for why it
// must stay separate from gChannelDemo above.
int gRing3ChannelDemo;

void procReceiverEntry() {
    u64 value = channelReceive(gChannelDemo);
    gReceiverValue = value;
    gReceiverGotMessage = true;
    while (true) {
        yield();
    }
}
