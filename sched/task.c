// A fixed table of kernel tasks, each with its own stack, round-robin
// scheduled. Switching happens both voluntarily (a task calls yield())
// and preemptively (the timer interrupt calls it too - see isr.c and
// switch.s's comments for how that works on top of the same mechanism).
//
// Task 0 is special: it's not created via create_task() - it's whatever
// context called scheduler_init() (kmain.c's _start), captured the first
// time IT calls yield(). Every other task's initial stack is hand-built
// in create_task() to look exactly like what switch_context() would have
// left behind, so the very first switch into it lands on `entry` via an
// ordinary `ret` - a fake "return address" baked into a fresh stack.
//
// sleep_ticks() adds real blocking on top of that: a task can take
// itself out of the round-robin entirely until a given tick. yield()
// skips blocked tasks when picking who runs next, waking one up
// (clearing `blocked`) the moment its wake tick arrives.
// (Named sleep_ticks, not sleep, to avoid any name collision with a
// POSIX libc function of that name, even though nothing here links libc.)
//
// Milestone 37: `used` (previously write-only - set true at creation,
// never read) now has real meaning too: syscall.c's process_exit()
// clears it on the calling task's own slot before yielding away for the
// last time, and yield()'s scan skips a cleared slot permanently, the
// same way it already skips a blocked one - just with no wake condition
// that will ever fire. Resources (frames, the process/task table slot
// itself) are deliberately NOT reclaimed yet - see README's Known
// Limitations.

#include "task.h"
#include "../mm/heap.h"
#include "../mm/frames.h"
#include "../mm/paging.h"
#include "../isr/isr.h"
#include "../proc/channel.h"

#pragma GCC visibility push(hidden)
extern void switch_context(u64* old_rsp_out, u64 new_rsp);
#pragma GCC visibility pop

task g_tasks[16];
int g_task_count;
int g_current_task;

void scheduler_init(void) {
    g_tasks[0].used = true;
    g_tasks[0].cr3 = g_pml4_phys;  // the shell/main loop keeps using the boot-time kernel space
    g_task_count = 1;
    g_current_task = 0;
}

bool create_task_with_cr3(void (*entry)(void), u64 cr3) {
    if (g_task_count >= 16) {
        return false;
    }
    u8* stack_mem = (u8*) kalloc(16384);
    if (stack_mem == NULL) {
        return false;
    }
    u64 stack_top = ((u64) stack_mem + 16384) & ~((u64) 15);

    u64* sp = (u64*) stack_top;
    sp = sp - 1; *sp = (u64) entry;  // fake return address - switch_context's `ret` lands here
    sp = sp - 1; *sp = 0;  // rbp
    sp = sp - 1; *sp = 0;  // rbx
    sp = sp - 1; *sp = 0;  // r12
    sp = sp - 1; *sp = 0;  // r13
    sp = sp - 1; *sp = 0;  // r14
    sp = sp - 1; *sp = 0;  // r15

    task* t = &g_tasks[g_task_count];
    t->saved_rsp = (u64) sp;
    t->used = true;
    t->cr3 = cr3;
    t->process_index = -1;    // plain kernel task by default - 0 would wrongly look like g_processes[0]
    t->waiting_channel = -1;  // not waiting on a channel by default - 0 would wrongly look like g_channels[0]
    // This task's own kalloc'd stack, reused as its private TSS.RSP0
    // target. Safe even for non-ring3 tasks (yield() only ever loads it
    // into RSP0 for a ring3-capable task) and safe for ring3 tasks
    // specifically because once process_entry_trampoline() ->
    // run_ring3_test() iretqs into ring3, this stack is never touched by
    // ordinary execution again (run_ring3_test never returns) - so
    // there's nothing left on it worth preserving the moment a ring3->
    // ring0 transition needs a fresh reset point.
    t->kernel_stack_top = stack_top;
    g_task_count = g_task_count + 1;
    return true;
}

// Every task before per-process address spaces existed (task1-4) shares
// the kernel's own address space - identical behavior to before that.
bool create_task(void (*entry)(void)) {
    return create_task_with_cr3(entry, g_pml4_phys);
}

// A task with its own private address space (see paging.c's
// clone_address_space()) - kernel code/heap stay reachable exactly as
// before, but anything this task maps at or past 0x80000000 is genuinely
// private to it.
bool create_isolated_task(void (*entry)(void)) {
    u64 cr3 = clone_address_space();
    if (cr3 == 0) {
        return false;
    }
    return create_task_with_cr3(entry, cr3);
}

void yield(void) {
    int prev = g_current_task;
    int next = prev;
    int scanned = 0;
    // Walk the ring looking for a runnable task, waking up anything
    // whose wake condition is satisfied as we pass it - task 0 (the
    // shell/main loop) never blocks itself, so this is guaranteed to
    // terminate with *something* runnable (at worst, right back at
    // `prev`) rather than looping forever with nothing to switch to.
    //
    // Two different wake conditions share this one scan: a plain
    // sleep_ticks() waits for a tick, a channel_receive() waits for a
    // message - waiting_channel is how a blocked task says which one it
    // means.
    while (scanned < g_task_count) {
        next = (next + 1) % g_task_count;
        scanned = scanned + 1;
        task* candidate = &g_tasks[next];
        if (!candidate->used) {
            continue;  // exited (milestone 37's process_exit()) - never runnable again
        }
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
    if (next == prev || g_tasks[next].blocked || !g_tasks[next].used) {
        return;  // nothing else runnable right now - keep running prev
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
    // Give the incoming task its own TSS.RSP0 if it's ring3-capable -
    // see set_tss_rsp0's comment (mm/paging.c) for why a single shared
    // RSP0 broke the moment more than one ring3 process could exist at
    // once. Harmless to skip for a plain kernel task: RSP0 is only ever
    // consulted on a ring3->ring0 transition, which can't happen for a
    // task that's never in ring3 to begin with.
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
void sleep_ticks(u64 ticks) {
    task* self = &g_tasks[g_current_task];
    self->blocked = true;
    self->wake_tick = g_tick_count + ticks;
    yield();
}

// Blocks the calling task until a message arrives on channel_index -
// marks itself blocked with waiting_channel set (instead of
// sleep_ticks()'s wake_tick) and yields away, exactly the shape
// sleep_ticks() already has just above. Never returns until a message
// is actually available; the `while` (not `if`) is defensive rather
// than load-bearing on this single-core scheduler - yield()'s scan only
// ever clears `blocked` once channel_has_message() is already true, so
// by the time control returns here the condition has always already
// been met.
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
// iteration); task3 deliberately never calls yield() at all - a tight
// busy loop. If preemption is really working, task3's counter should
// still climb (in big jumps - it spins for its *whole* timer slice each
// turn, unlike task1/task2's one-increment turns) *and* task1/task2/the
// shell should keep advancing too, proving the timer forces control
// away from it.

// task4 is the blocking demo: instead of spinning or yielding every
// iteration, it sleeps for 50 ticks (~500ms at the PIT's 100Hz) between
// increments, so its counter should climb at roughly g_tick_count/50 -
// dramatically slower than task1/task2 and worlds slower than task3.

u64 g_task1_ticks;
u64 g_task2_ticks;
u64 g_task3_ticks;
u64 g_task4_ticks;

void task1_entry(void) {
    for (;;) {
        g_task1_ticks = g_task1_ticks + 1;
        yield();
    }
}

void task2_entry(void) {
    for (;;) {
        g_task2_ticks = g_task2_ticks + 1;
        yield();
    }
}

void task3_entry(void) {
    for (;;) {
        g_task3_ticks = g_task3_ticks + 1;
    }
}

void task4_entry(void) {
    for (;;) {
        g_task4_ticks = g_task4_ticks + 1;
        sleep_ticks(50);
    }
}

// ---- Isolation demo: two isolated "processes" both map the SAME
// virtual address (well past anything else this kernel uses) in their
// OWN private address space, each to a physical frame nobody else knows
// about, and write a distinct, recognizable constant there. If CR3
// switching + per-process PDPT[2..] isolation genuinely works, each
// keeps reading back its own value forever and their two demo pages
// resolve to different physical addresses.

u64 g_demo_vaddr = 0x80000000;  // 2GB - PDPT index 2, untouched by any earlier subsystem
u32 g_proc_a_value;
u32 g_proc_b_value;
u64 g_proc_a_phys;
u64 g_proc_b_phys;

void proc_a_entry(void) {
    void* frame = alloc_frame();
    map_page_in(g_tasks[g_current_task].cr3, g_demo_vaddr, (u64) frame, 0x06 | PAGE_NX);  // writable + user, non-executable
    u32* p = (u32*) g_demo_vaddr;
    *p = 0xAAAAAAAA;
    for (;;) {
        g_proc_a_value = *p;
        g_proc_a_phys = translate_in(g_tasks[g_current_task].cr3, g_demo_vaddr);
        yield();
    }
}

void proc_b_entry(void) {
    void* frame = alloc_frame();
    map_page_in(g_tasks[g_current_task].cr3, g_demo_vaddr, (u64) frame, 0x06 | PAGE_NX);  // writable + user, non-executable
    u32* p = (u32*) g_demo_vaddr;
    *p = 0xBBBBBBBB;
    for (;;) {
        g_proc_b_value = *p;
        g_proc_b_phys = translate_in(g_tasks[g_current_task].cr3, g_demo_vaddr);
        yield();
    }
}

// ---- IPC demo: an isolated process blocked on channel_receive() since
// boot, with the shell itself as the sender (the `send` command,
// shell.c) rather than a second task on a timer - deliberately, so the
// "still blocked" and "just woke up" states are operator-triggered and
// exactly reproducible instead of racing QEMU/TCG's timer.

int g_channel_demo;
bool g_receiver_got_message;
u64 g_receiver_value;

// A second, dedicated channel index for the ring3 program's own
// Channel.receive() - must stay separate from g_channel_demo above.
int g_ring3_channel_demo;

void proc_receiver_entry(void) {
    u64 value = channel_receive(g_channel_demo);
    g_receiver_value = value;
    g_receiver_got_message = true;
    for (;;) {
        yield();
    }
}
