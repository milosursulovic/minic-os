// Fixed task table, round-robin scheduled, cooperative + preemptive
// (timer ISR also calls yield()). Task 0 is whatever called
// scheduler_init(), never created via create_task(). Named sleep_ticks,
// not sleep, to avoid colliding with a POSIX libc name.

#include "task.h"
#include "../mm/heap.h"
#include "../mm/frames.h"
#include "../mm/paging.h"
#include "../isr/isr.h"
#include "../../proc/ipc/channel.h"
#include "../../proc/ipc/io_request.h"
#include "../../proc/ipc/net_request.h"
#include "../../proc/ipc/net_tcp_request.h"

#pragma GCC visibility push(hidden)
extern void switch_context(u64* old_rsp_out, u64 new_rsp);
#pragma GCC visibility pop

task g_tasks[MAX_TASKS];
int g_task_count;
int g_current_task;

void scheduler_init(void) {
    g_tasks[0].used = true;
    g_tasks[0].cr3 = g_pml4_phys;  // the shell/main loop keeps using the boot-time kernel space
    g_task_count = 1;
    g_current_task = 0;
}

// Reuses an exited slot (and its already-allocated stack - no kalloc,
// no leak) if one exists, else appends a fresh one.
int create_task_with_cr3(void (*entry)(void), u64 cr3) {
    int index = -1;
    int i = 0;
    while (i < g_task_count) {
        if (!g_tasks[i].used) {
            index = i;
            break;
        }
        i = i + 1;
    }

    u64 stack_top;
    if (index >= 0) {
        stack_top = g_tasks[index].kernel_stack_top;
    } else {
        if (g_task_count >= MAX_TASKS) {
            return -1;
        }
        u8* stack_mem = (u8*) kalloc(16384);
        if (stack_mem == NULL) {
            return -1;
        }
        stack_top = ((u64) stack_mem + 16384) & ~((u64) 15);
        index = g_task_count;
        g_task_count = g_task_count + 1;
    }

    u64* sp = (u64*) stack_top;
    sp = sp - 1; *sp = (u64) entry;  // fake return address for switch_context's ret
    sp = sp - 1; *sp = 0;  // rbp
    sp = sp - 1; *sp = 0;  // rbx
    sp = sp - 1; *sp = 0;  // r12
    sp = sp - 1; *sp = 0;  // r13
    sp = sp - 1; *sp = 0;  // r14
    sp = sp - 1; *sp = 0;  // r15

    task* t = &g_tasks[index];
    t->saved_rsp = (u64) sp;
    t->used = true;
    t->blocked = false;
    t->wake_tick = 0;
    t->cr3 = cr3;
    t->ring3_entry_vaddr = 0;
    t->ring3_user_stack_top = 0;
    t->process_index = -1;    // -1, not 0, so it can't look like g_processes[0]
    t->waiting_on = NULL;
    t->kernel_stack_top = stack_top;  // reused as this task's TSS.RSP0 target
    return index;
}

bool create_task(void (*entry)(void)) {
    return create_task_with_cr3(entry, g_pml4_phys) >= 0;
}

bool create_isolated_task(void (*entry)(void)) {
    u64 cr3 = clone_address_space();
    if (cr3 == 0) {
        return false;
    }
    return create_task_with_cr3(entry, cr3) >= 0;
}

void yield(void) {
    int prev = g_current_task;
    int next = prev;
    int scanned = 0;
    // Task 0 never blocks itself, so this always terminates with
    // something runnable.
    while (scanned < g_task_count) {
        next = (next + 1) % g_task_count;
        scanned = scanned + 1;
        task* candidate = &g_tasks[next];
        if (!candidate->used) {
            continue;  // exited - never runnable again
        }
        if (candidate->blocked) {
            bool wake;
            if (candidate->waiting_on != NULL) {
                wake = *(candidate->waiting_on);
            } else {
                wake = g_tick_count >= candidate->wake_tick;
            }
            if (wake) {
                candidate->blocked = false;
                candidate->waiting_on = NULL;
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
    // Safe before the stack switch: every address space shares PDPT[0]/[1].
    load_cr3(next_task->cr3);
    if (next_task->ring3_entry_vaddr != 0) {
        set_tss_rsp0(next_task->kernel_stack_top);
    }
    switch_context(&prev_task->saved_rsp, next_task->saved_rsp);
    // Resumes here once some other task switches back.
}

void sleep_ticks(u64 ticks) {
    task* self = &g_tasks[g_current_task];
    self->blocked = true;
    self->wake_tick = g_tick_count + ticks;
    yield();
}

u64 channel_receive(int channel_index) {
    while (!channel_has_message(channel_index)) {
        task* self = &g_tasks[g_current_task];
        self->blocked = true;
        self->waiting_on = &g_channels[channel_index].full;
        yield();
    }
    u64 value = g_channels[channel_index].message;
    g_channels[channel_index].full = false;
    return value;
}

void io_request_wait(int slot_index) {
    while (!g_io_requests[slot_index].done) {
        task* self = &g_tasks[g_current_task];
        self->blocked = true;
        self->waiting_on = &g_io_requests[slot_index].done;
        yield();
    }
}

void net_ping_request_wait(int slot_index) {
    while (!g_net_ping_requests[slot_index].done) {
        task* self = &g_tasks[g_current_task];
        self->blocked = true;
        self->waiting_on = &g_net_ping_requests[slot_index].done;
        yield();
    }
}

void net_tcp_request_wait(int slot_index) {
    while (!g_net_tcp_requests[slot_index].done) {
        task* self = &g_tasks[g_current_task];
        self->blocked = true;
        self->waiting_on = &g_net_tcp_requests[slot_index].done;
        yield();
    }
}

// Demo tasks proving real switching: task1/2 cooperative, task3 a tight
// busy loop (no yield), task4 sleeps 50 ticks between increments.

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

// Isolation demo: two processes map the same vaddr, each to its own
// frame - proves per-process isolation if they never see each other's value.

u64 g_demo_vaddr = 0x80000000;
u32 g_proc_a_value;
u32 g_proc_b_value;
u64 g_proc_a_phys;
u64 g_proc_b_phys;

void proc_a_entry(void) {
    void* frame = alloc_frame();
    map_page_in(g_tasks[g_current_task].cr3, g_demo_vaddr, (u64) frame, 0x06 | PAGE_NX);
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
    map_page_in(g_tasks[g_current_task].cr3, g_demo_vaddr, (u64) frame, 0x06 | PAGE_NX);
    u32* p = (u32*) g_demo_vaddr;
    *p = 0xBBBBBBBB;
    for (;;) {
        g_proc_b_value = *p;
        g_proc_b_phys = translate_in(g_tasks[g_current_task].cr3, g_demo_vaddr);
        yield();
    }
}

// IPC demo: blocked on channel_receive() since boot; shell's `send`
// command is the sender.

int g_channel_demo;
bool g_receiver_got_message;
u64 g_receiver_value;

int g_ring3_channel_demo;

void proc_receiver_entry(void) {
    u64 value = channel_receive(g_channel_demo);
    g_receiver_value = value;
    g_receiver_got_message = true;
    for (;;) {
        yield();
    }
}
