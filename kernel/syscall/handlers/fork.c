#include "fork.h"
#include "../../sched/task.h"
#include "../../mm/paging/paging.h"
#include "../../../proc/process.h"
#include "../../../proc/ipc/object/object.h"

#pragma GCC visibility push(hidden)
extern void fork_enter_ring3(u64* regs);
#pragma GCC visibility pop

// One pending register snapshot per task slot - consulted exactly once,
// by fork_resume_trampoline() below, the moment a freshly-fork()'d
// child's task is first scheduled. See fork_enter_ring3.s for the
// layout (index 0..19).
static u64 g_fork_pending_regs[MAX_TASKS][20];

// Real trap-frame layout kernel/isr/interrupts.s's isr_syscall stub
// pushes onto the CURRENT task's own kernel_stack_top (its TSS.RSP0
// target) before calling syscall_dispatch - fixed, documented offsets
// (interrupts.s's own epilogue comments document the same block from
// the other end, e.g. "[rsp+112]=orig rax"). Reading it this way (from
// the stack's fixed top, not a live rsp) needs zero changes to that
// shared asm stub - every other syscall is completely unaffected.
#define TRAPFRAME_SS_OFF      8
#define TRAPFRAME_RSP_OFF     16
#define TRAPFRAME_RFLAGS_OFF  24
#define TRAPFRAME_CS_OFF      32
#define TRAPFRAME_RIP_OFF     40
#define TRAPFRAME_RAX_OFF     48
#define TRAPFRAME_RBX_OFF     56
#define TRAPFRAME_RCX_OFF     64
#define TRAPFRAME_RDX_OFF     72
#define TRAPFRAME_RSI_OFF     80
#define TRAPFRAME_RDI_OFF     88
#define TRAPFRAME_RBP_OFF     96
#define TRAPFRAME_R8_OFF      104
#define TRAPFRAME_R9_OFF      112
#define TRAPFRAME_R10_OFF     120
#define TRAPFRAME_R11_OFF     128
#define TRAPFRAME_R12_OFF     136
#define TRAPFRAME_R13_OFF     144
#define TRAPFRAME_R14_OFF     152
#define TRAPFRAME_R15_OFF     160

static u64 read_trapframe(u64 kernel_stack_top, u64 offset) {
    return *(u64*) (kernel_stack_top - offset);
}

// Save/restore IF - same established pattern kernel/mm/frames/frames.c's
// alloc_frame()/proc/process.c's spawn_process() use, and for the exact
// same real bug: create_task_with_cr3() makes the child task schedulable
// immediately, but this function still has several steps left (snapshot
// capture, ring3_entry_vaddr, g_processes[proc_index], process_index
// back-link) before it's safe for the child to actually run. See
// process.c's own comment for the full stuck-respawn-bug story this
// closes for spawn_process(); the identical race exists here too.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

// This task's very first (and only) resume, if it's a fork() child -
// create_task_with_cr3()'s entry point. Reads this task's own stored
// snapshot and jumps into it via fork_enter_ring3.s. Never returns.
static void fork_resume_trampoline(void) {
    fork_enter_ring3(&g_fork_pending_regs[g_current_task][0]);
}

// Real copy-on-write fork() (Faza I point 4, item 8). The child resumes
// at the exact rip/rsp the parent was at when it called fork(), with
// every register matching except rax=0 (the child's own return value) -
// see fork_enter_ring3.s. The parent's own rax gets this function's
// normal *result (the child's task_index) through the existing,
// unchanged syscall return-value mechanism - no special-casing needed
// for the parent side at all.
bool syscall_fork(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    (void) a1;
    (void) a2;
    (void) a3;
    if (num == 95) {
        int parent_process = g_tasks[g_current_task].process_index;
        if (parent_process < 0) {
            *result = (u64) -1;
            return true;
        }

        int proc_index = -1;
        int p = 0;
        while (p < g_process_count) {
            if (!g_processes[p].used) {
                proc_index = p;
                break;
            }
            p = p + 1;
        }
        if (proc_index < 0 && g_process_count >= MAX_PROCESSES) {
            *result = (u64) -1;
            return true;
        }

        u64 child_cr3 = clone_address_space();
        if (child_cr3 == 0) {
            *result = (u64) -1;
            return true;
        }
        if (!clone_address_space_cow(g_processes[parent_process].cr3, child_cr3)) {
            free_address_space(child_cr3);
            *result = (u64) -1;
            return true;
        }

        // Critical section: the child task becomes schedulable the
        // instant create_task_with_cr3() sets its used=true - everything
        // below must finish before a timer-ISR preemption could let it
        // run with an incomplete process_index/register snapshot. See
        // this file's own disable_interrupts() comment above.
        u64 saved_flags = disable_interrupts();

        int task_index = create_task_with_cr3(&fork_resume_trampoline, child_cr3);
        if (task_index < 0) {
            restore_interrupts(saved_flags);
            free_address_space(child_cr3);
            *result = (u64) -1;
            return true;
        }

        // Capture the parent's own live ring3 register snapshot (this
        // task's kernel_stack_top, not the child's) before the child
        // ever runs - rax forced to 0.
        u64 kernel_stack_top = g_tasks[g_current_task].kernel_stack_top;
        u64* snap = &g_fork_pending_regs[task_index][0];
        snap[0] = 0;  // rax=0 - the child's own fork() return value
        snap[1] = read_trapframe(kernel_stack_top, TRAPFRAME_RBX_OFF);
        snap[2] = read_trapframe(kernel_stack_top, TRAPFRAME_RCX_OFF);
        snap[3] = read_trapframe(kernel_stack_top, TRAPFRAME_RDX_OFF);
        snap[4] = read_trapframe(kernel_stack_top, TRAPFRAME_RSI_OFF);
        snap[5] = read_trapframe(kernel_stack_top, TRAPFRAME_RDI_OFF);
        snap[6] = read_trapframe(kernel_stack_top, TRAPFRAME_RBP_OFF);
        snap[7] = read_trapframe(kernel_stack_top, TRAPFRAME_R8_OFF);
        snap[8] = read_trapframe(kernel_stack_top, TRAPFRAME_R9_OFF);
        snap[9] = read_trapframe(kernel_stack_top, TRAPFRAME_R10_OFF);
        snap[10] = read_trapframe(kernel_stack_top, TRAPFRAME_R11_OFF);
        snap[11] = read_trapframe(kernel_stack_top, TRAPFRAME_R12_OFF);
        snap[12] = read_trapframe(kernel_stack_top, TRAPFRAME_R13_OFF);
        snap[13] = read_trapframe(kernel_stack_top, TRAPFRAME_R14_OFF);
        snap[14] = read_trapframe(kernel_stack_top, TRAPFRAME_R15_OFF);
        snap[15] = read_trapframe(kernel_stack_top, TRAPFRAME_RIP_OFF);
        snap[16] = read_trapframe(kernel_stack_top, TRAPFRAME_CS_OFF);
        snap[17] = read_trapframe(kernel_stack_top, TRAPFRAME_RFLAGS_OFF);
        snap[18] = read_trapframe(kernel_stack_top, TRAPFRAME_RSP_OFF);
        snap[19] = read_trapframe(kernel_stack_top, TRAPFRAME_SS_OFF);

        // Real bug caught before ever booting this: kernel/sched/task.c's
        // yield() only calls set_tss_rsp0(next_task->kernel_stack_top)
        // when next_task->ring3_entry_vaddr != 0 - it's the scheduler's
        // own proxy for "this task is ring3-capable, keep its TSS.RSP0
        // pointed at its own kernel stack". fork_resume_trampoline()
        // doesn't actually consume ring3_entry_vaddr (it jumps via the
        // captured snapshot instead), but leaving it at the default 0
        // would make the scheduler skip this child's TSS.RSP0 updates
        // forever - its first real syscall/interrupt after resuming
        // would trap onto whatever OTHER task's kernel stack happened to
        // be set last, corrupting it. Set to the real captured rip -
        // truthy, and honestly describes where this task runs.
        g_tasks[task_index].ring3_entry_vaddr = snap[15];

        // Same bookkeeping order spawn_process() (proc/process.c) already
        // establishes: task first, then the process slot, then wire the
        // task back to it last.
        if (proc_index < 0) {
            proc_index = g_process_count;
            g_process_count = g_process_count + 1;
        }
        g_processes[proc_index].used = true;
        g_processes[proc_index].cr3 = child_cr3;
        g_processes[proc_index].task_index = task_index;
        g_processes[proc_index].uid = g_processes[parent_process].uid;
        g_processes[proc_index].main_stack_vaddr = g_processes[parent_process].main_stack_vaddr;
        g_processes[proc_index].next_thread_stack_vaddr = g_processes[parent_process].next_thread_stack_vaddr;
        g_tasks[task_index].process_index = proc_index;

        // Stated, honest scope limit: a fresh, empty handle table (handle
        // 0 = self only), not a duplicate of the parent's open handles -
        // real fd/handle inheritance is a separate concern (point 8's
        // IPC semantics), out of scope for this item's memory-
        // duplication focus (point 4).
        int self_object = alloc_object(OBJ_PROCESS, proc_index);
        alloc_handle(proc_index, self_object, RIGHT_QUERY);

        restore_interrupts(saved_flags);
        *result = (u64) task_index;
        return true;
    }
    return false;
}
