// A fixed table of kernel tasks, each with its own stack, round-robin
// scheduled. Switching happens both voluntarily (a task calls yield())
// and preemptively (the timer interrupt calls it too - see isr.mc and
// switch.s's comments for how that works on top of the same mechanism).
//
// Task 0 is special: it's not created via create_task() - it's whatever
// context called scheduler_init() (kmain.mc's _start), captured the first
// time IT calls yield(). Every other task's initial stack is hand-built
// in create_task() to look exactly like what switch_context() would have
// left behind, so the very first switch into it lands on `entry` via an
// ordinary `ret` - a fake "return address" baked into a fresh stack.
//
// sleep() adds real blocking on top of that: a task can take itself out
// of the round-robin entirely until a given tick, instead of the only
// two options being "spin forever" (task3_entry) or "yield every
// iteration and get scheduled again immediately" (task1_entry/
// task2_entry). yield() skips blocked tasks when picking who runs next,
// waking one up (clearing `blocked`) the moment its wake tick arrives.

import "../mm/heap.mc";
import "../mm/frames.mc";
import "../mm/paging.mc";
import "../isr/isr.mc";
import "../proc/channel.mc";

extern void switch_context(u64* old_rsp_out, u64 new_rsp);

struct task {
    u64 saved_rsp;
    bool used;
    bool blocked;
    u64 wake_tick;   // only meaningful while blocked
    u64 cr3;        // this task's address space (milestone 12)
    u64 ring3_entry_vaddr;    // 0 = plain kernel task; else jump here via run_ring3_test (milestone 13)
    u64 ring3_user_stack_top;  // only meaningful when ring3_entry_vaddr != 0
    int process_index;       // -1 = plain kernel task; else an index into g_processes[] (milestone 14)
    int waiting_channel;     // -1 = blocked on a tick (wake_tick), else blocked on g_channels[this] (milestone 15)
    u64 kernel_stack_top;     // this task's own TSS.RSP0 target (milestone 19) - see set_tss_rsp0's comment
}

// Sized with headroom past what boots today (task 0 + task1-4 + proc_a/
// proc_b + the spawned ring3 process + the channel demo's sender/
// receiver = 10) - task structs are cheap (no heap cost, each task's
// real 16KB stack is a separate kalloc()), so oversizing this array
// costs nothing but a few dozen bytes of zeroed .bss.
task g_tasks[16];
int g_task_count;
int g_current_task;

void scheduler_init() {
    g_tasks[0].used = true;
    g_tasks[0].cr3 = g_pml4_phys;   // the shell/main loop keeps using the boot-time kernel space
    g_task_count = 1;
    g_current_task = 0;
}

bool create_task_with_cr3(fn() -> void entry, u64 cr3) {
    if (g_task_count >= 16) {
        return false;
    }
    u8* stack_mem = (u8*) kalloc(16384);
    if (stack_mem == null) {
        return false;
    }
    u64 stack_top = ((u64) stack_mem + 16384) & ~((u64) 15);

    u64* sp = (u64*) stack_top;
    sp = sp - 1; *sp = (u64) entry;   // fake return address - switch_context's `ret` lands here
    sp = sp - 1; *sp = 0;   // rbp
    sp = sp - 1; *sp = 0;   // rbx
    sp = sp - 1; *sp = 0;   // r12
    sp = sp - 1; *sp = 0;   // r13
    sp = sp - 1; *sp = 0;   // r14
    sp = sp - 1; *sp = 0;   // r15

    task* t = &g_tasks[g_task_count];
    t->saved_rsp = (u64) sp;
    t->used = true;
    t->cr3 = cr3;
    t->process_index = -1;   // plain kernel task by default - 0 would wrongly look like g_processes[0]
    t->waiting_channel = -1;   // not waiting on a channel by default - 0 would wrongly look like g_channels[0]
    // This task's own kalloc'd stack, reused as its private TSS.RSP0
    // target (milestone 19). Safe even for non-ring3 tasks (yield()
    // only ever loads it into RSP0 for a ring3-capable task) and safe
    // for ring3 tasks specifically because once process_entry_trampoline()
    // -> run_ring3_test() iretqs into ring3, this stack is never touched
    // by ordinary execution again (run_ring3_test never returns) - so
    // there's nothing left on it worth preserving the moment a ring3->
    // ring0 transition needs a fresh reset point.
    t->kernel_stack_top = stack_top;
    g_task_count = g_task_count + 1;
    return true;
}

// Every task before milestone 12 (task1-4) shares the kernel's own
// address space - identical behavior to before this milestone existed.
bool create_task(fn() -> void entry) {
    return create_task_with_cr3(entry, g_pml4_phys);
}

// A task with its own private address space (see paging.mc's
// clone_address_space()) - kernel code/heap stay reachable exactly as
// before, but anything this task maps at or past 0x80000000 is genuinely
// private to it.
bool create_isolated_task(fn() -> void entry) {
    u64 cr3 = clone_address_space();
    if (cr3 == 0) {
        return false;
    }
    return create_task_with_cr3(entry, cr3);
}

void yield() {
    int prev = g_current_task;
    int next = prev;
    int scanned = 0;
    // Walk the ring looking for a runnable task, waking up anything
    // whose wake condition is satisfied as we pass it - task 0 (the
    // shell/main loop) never blocks itself, so this is guaranteed to
    // terminate with *something* runnable (at worst, right back at
    // `prev`) rather than looping forever with nothing to switch to.
    //
    // Two different wake conditions share this one scan (milestone 15):
    // a plain sleep() waits for a tick, a channel_receive() waits for a
    // message - waiting_channel is how a blocked task says which one it
    // means. This is the exact generalization the roadmap called for:
    // reusing sleep()'s blocking mechanism for IPC, not inventing a
    // second one next to it.
    while (scanned < g_task_count) {
        next = (next + 1) % g_task_count;
        scanned = scanned + 1;
        task* candidate = &g_tasks[next];
        if (candidate->blocked) {
            bool wake = false;
            if (candidate->waiting_channel >= 0) {
                wake = channel_has_message(candidate->waiting_channel);
            } else {
                wake = g_tick_count >= candidate->wake_tick;
            }
            if (wake) {
                candidate->blocked = false;
                candidate->waiting_channel = -1;
            }
        }
        if (!candidate->blocked) {
            break;
        }
    }
    if (next == prev || g_tasks[next].blocked) {
        return;   // nothing else runnable right now - keep running prev
    }
    g_current_task = next;
    task* prev_task = &g_tasks[prev];
    task* next_task = &g_tasks[next];
    // Loading CR3 here, before the stack switch, is safe even though
    // we're still running on prev_task's stack for one more instruction:
    // clone_address_space() guarantees every task's PDPT[0]/PDPT[1] (all
    // kernel code, the current stack, the heap) resolve identically
    // across every address space - only the private per-process region
    // (vaddr >= 0x80000000) can differ, and nothing here touches that.
    load_cr3(next_task->cr3);
    // Milestone 19: give the incoming task its own TSS.RSP0 if it's
    // ring3-capable - see set_tss_rsp0's comment (mm/paging.mc) for why a
    // single shared RSP0 broke the moment more than one ring3 process
    // could exist at once. Harmless to skip for a plain kernel task:
    // RSP0 is only ever consulted on a ring3->ring0 transition, which
    // can't happen for a task that's never in ring3 to begin with, so
    // leaving whatever the last ring3 task's RSP0 was in place costs
    // nothing.
    if (next_task->ring3_entry_vaddr != 0) {
        set_tss_rsp0(next_task->kernel_stack_top);
    }
    switch_context(&prev_task->saved_rsp, next_task->saved_rsp);
    // Execution resumes here once some other task switches back to this
    // one. Interrupts are already back on by this point - see switch.s's
    // comment for why the `sti` lives there and not here.
}

// Takes the calling task out of the round-robin until at least `ticks`
// timer ticks from now. Must be called from the task's own context (it
// blocks g_current_task, then immediately yields away from itself).
void sleep(u64 ticks) {
    task* self = &g_tasks[g_current_task];
    self->blocked = true;
    self->wake_tick = g_tick_count + ticks;
    yield();
}

// Blocks the calling task until a message arrives on channel_index -
// marks itself blocked with waiting_channel set (instead of sleep()'s
// wake_tick) and yields away, exactly the shape sleep() already has just
// above. Never returns until a message is actually available; the
// `while` (not `if`) is defensive rather than load-bearing on this
// single-core scheduler - yield()'s scan only ever clears `blocked`
// once channel_has_message() is already true, so by the time control
// returns here the condition has always already been met.
u64 channel_receive(int channel_index) {
    while (!channel_has_message(channel_index)) {
        task* self = &g_tasks[g_current_task];
        self->blocked = true;
        self->waiting_channel = channel_index;
        yield();
    }
    u64 value = g_channels[channel_index].message;
    g_channels[channel_index].full = false;
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
// increments, so its counter should climb at roughly g_tick_count/50 -
// dramatically slower than task1/task2 (once per full ring rotation,
// effectively every timer tick) and worlds slower than task3 (millions
// per turn). If sleep()/the blocked-skipping logic in yield() were
// broken, task4 would either never advance at all (stuck blocked
// forever) or advance at the same rate as task1/task2 (blocking a no-op,
// same as an immediate yield()) - neither of which would track
// g_tick_count/50.

u64 g_task1_ticks;
u64 g_task2_ticks;
u64 g_task3_ticks;
u64 g_task4_ticks;

void task1_entry() {
    while (true) {
        g_task1_ticks = g_task1_ticks + 1;
        yield();
    }
}

void task2_entry() {
    while (true) {
        g_task2_ticks = g_task2_ticks + 1;
        yield();
    }
}

void task3_entry() {
    while (true) {
        g_task3_ticks = g_task3_ticks + 1;
    }
}

void task4_entry() {
    while (true) {
        g_task4_ticks = g_task4_ticks + 1;
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
// recently, and translate_in() would show the SAME physical address for
// both, since there'd only really be one page table entry for that vaddr
// in the whole system either way.

u64 g_demo_vaddr = 0x80000000;   // 2GB - PDPT index 2, untouched by any earlier milestone
u32 g_proc_a_value;
u32 g_proc_b_value;
u64 g_proc_a_phys;
u64 g_proc_b_phys;

void proc_a_entry() {
    void* frame = alloc_frame();
    map_page_in(g_tasks[g_current_task].cr3, g_demo_vaddr, (u64) frame, 0x06 | page_nx);   // writable + user, non-executable (milestone 28)
    u32* p = (u32*) g_demo_vaddr;
    *p = 0xAAAAAAAA;
    while (true) {
        g_proc_a_value = *p;
        g_proc_a_phys = translate_in(g_tasks[g_current_task].cr3, g_demo_vaddr);
        yield();
    }
}

void proc_b_entry() {
    void* frame = alloc_frame();
    map_page_in(g_tasks[g_current_task].cr3, g_demo_vaddr, (u64) frame, 0x06 | page_nx);   // writable + user, non-executable (milestone 28)
    u32* p = (u32*) g_demo_vaddr;
    *p = 0xBBBBBBBB;
    while (true) {
        g_proc_b_value = *p;
        g_proc_b_phys = translate_in(g_tasks[g_current_task].cr3, g_demo_vaddr);
        yield();
    }
}

// ---- Milestone 15 demo: an isolated process blocked on channel_receive()
// since boot, with the shell itself as the sender (the `send` command,
// shell.mc) rather than a second task on a timer - deliberately, so the
// "still blocked" and "just woke up" states are operator-triggered and
// exactly reproducible instead of racing QEMU/TCG's timer (observed to
// run at wildly varying effective rates across sessions - the same
// gotcha the kernel-qemu-test skill already documents for g_tick_count
// comparisons, now hit for a fixed sleep() duration too, not just a
// ticks-elapsed reading). proc_receiver_entry calls channel_receive()
// immediately at boot, long before any `send` is ever typed - it
// genuinely blocks (every yield() scan skips it while the channel stays
// empty), for as long as an operator chooses to run other commands
// first, not just briefly. g_receiver_got_message/g_receiver_value are only
// ever written after channel_receive() actually returns, so `chan`
// reading false across an arbitrary number of other commands, then true
// with the exact sent value the instant `send` runs, is the whole
// proof: genuinely still blocked (not a no-op, not a short timeout),
// and the message really crossed from the shell's own address space
// into this isolated process's (a separate cr3, same as proc_a/proc_b)
// through the channel, not shared memory.

int g_channel_demo;
bool g_receiver_got_message;
u64 g_receiver_value;

// Milestone 23: a second, dedicated channel index for the ring3
// program's own Channel.receive() - see kmain.mc's comment for why it
// must stay separate from g_channel_demo above.
int g_ring3_channel_demo;

void proc_receiver_entry() {
    u64 value = channel_receive(g_channel_demo);
    g_receiver_value = value;
    g_receiver_got_message = true;
    while (true) {
        yield();
    }
}
