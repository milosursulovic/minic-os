// A minimal cooperative scheduler: a fixed table of kernel tasks, each
// with its own stack, switching between them only when a task calls
// yield() - not preemptive yet (the timer interrupt still just ticks and
// returns to whatever was running; see README for what a preemptive
// version would need on top of this).
//
// Task 0 is special: it's not created via createTask() - it's whatever
// context called schedulerInit() (kmain.mc's _start), captured the first
// time IT calls yield(). Every other task's initial stack is hand-built
// in createTask() to look exactly like what switch_context() would have
// left behind, so the very first switch into it lands on `entry` via an
// ordinary `ret` - a fake "return address" baked into a fresh stack.

import "../mm/heap.mc";

extern void switch_context(u64* oldRspOut, u64 newRsp);

struct Task {
    u64 savedRsp;
    bool used;
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
    int next = (gCurrentTask + 1) % gTaskCount;
    if (next == prev) {
        return;   // only one task registered - nothing to switch to
    }
    gCurrentTask = next;
    Task* prevTask = &gTasks[prev];
    Task* nextTask = &gTasks[next];
    switch_context(&prevTask->savedRsp, nextTask->savedRsp);
    // Execution resumes here once some other task switches back to this
    // one. Interrupts are already back on by this point - see switch.s's
    // comment for why the `sti` lives there and not here.
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

u64 gTask1Ticks;
u64 gTask2Ticks;
u64 gTask3Ticks;

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
