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
import "../isr/isr.mc";

extern void switch_context(u64* oldRspOut, u64 newRsp);

struct Task {
    u64 savedRsp;
    bool used;
    bool blocked;
    u64 wakeTick;   // only meaningful while blocked
}

Task gTasks[8];
int gTaskCount;
int gCurrentTask;

void schedulerInit() {
    gTasks[0].used = true;
    gTaskCount = 1;
    gCurrentTask = 0;
}

bool createTask(fn() -> void entry) {
    if (gTaskCount >= 8) {
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
    gTaskCount = gTaskCount + 1;
    return true;
}

void yield() {
    int prev = gCurrentTask;
    int next = prev;
    int scanned = 0;
    // Walk the ring looking for a runnable task, waking up anything
    // whose sleep has expired as we pass it - task 0 (the shell/main
    // loop) never blocks itself, so this is guaranteed to terminate with
    // *something* runnable (at worst, right back at `prev`) rather than
    // looping forever with nothing to switch to.
    while (scanned < gTaskCount) {
        next = (next + 1) % gTaskCount;
        scanned = scanned + 1;
        Task* candidate = &gTasks[next];
        if (candidate->blocked && gTickCount >= candidate->wakeTick) {
            candidate->blocked = false;
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
