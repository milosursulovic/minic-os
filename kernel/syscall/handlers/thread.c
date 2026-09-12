#include "thread.h"
#include "../../sched/task.h"
#include "../../mm/frames/frames.h"
#include "../../mm/paging/paging.h"
#include "../../../proc/ipc/object/object.h"
#include "../../../proc/process.h"

// Save/restore IF - same established pattern proc/process.c's
// spawn_process() and kernel/syscall/handlers/fork.c's syscall_fork()
// use, and for the identical real bug: create_task_with_cr3() makes the
// new task schedulable (used=true) the instant it returns, but this
// function still has several steps left (ring3_entry_vaddr,
// ring3_user_stack_top, process_index, handle) before it's safe for that
// task to actually run - a preemption landing in that gap would let the
// scheduler resume it with ring3_entry_vaddr still 0, which yield() reads
// as "not ring3-capable" and skips its TSS.RSP0 update entirely, leaving
// TSS.RSP0 pointed at whichever task was running before. The new task's
// own first ring3->ring0 transition then pushes its trapframe onto THAT
// other (unrelated, still-live) task's kernel stack - corrupting it.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

// Real Thread object (Faza I point 3) - a second task sharing the
// calling process's own cr3 (create_task_with_cr3 already accepts an
// arbitrary cr3 - no scheduler changes needed), given its own private
// stack page since that can't be shared. See object.h's OBJ_THREAD
// and proc/process.h's main_stack_vaddr/next_thread_stack_vaddr.
bool syscall_thread(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    (void) a2;
    (void) a3;
    if (num == 71) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        process* p = &g_processes[caller_process];

        u64 saved_flags = disable_interrupts();

        int task_index = create_task_with_cr3(&process_entry_trampoline, p->cr3);
        if (task_index < 0) {
            restore_interrupts(saved_flags);
            *result = (u64) -1;
            return true;
        }
        void* frame = alloc_frame();
        if (frame == NULL) {
            g_tasks[task_index].used = false;
            restore_interrupts(saved_flags);
            *result = (u64) -1;
            return true;
        }
        u64 stack_vaddr = p->next_thread_stack_vaddr;
        // Writable + user + NX, same flags spawn_process() already uses
        // for the main thread's own stack - never executable.
        if (!map_page_in(p->cr3, stack_vaddr, (u64) frame, 0x06 | PAGE_NX)) {
            free_frame(frame);
            g_tasks[task_index].used = false;
            restore_interrupts(saved_flags);
            *result = (u64) -1;
            return true;
        }
        p->next_thread_stack_vaddr = p->next_thread_stack_vaddr + 0x10000;
        g_tasks[task_index].ring3_entry_vaddr = a1;
        g_tasks[task_index].ring3_user_stack_top = stack_vaddr + 4096;
        g_tasks[task_index].process_index = caller_process;
        int obj = alloc_object(OBJ_THREAD, task_index);
        if (obj < 0) {
            g_tasks[task_index].used = false;
            restore_interrupts(saved_flags);
            *result = (u64) -1;
            return true;
        }
        int h = alloc_handle(caller_process, obj, RIGHT_QUERY);
        if (h < 0) {
            free_object(obj);
            g_tasks[task_index].used = false;
            restore_interrupts(saved_flags);
            *result = (u64) -1;
            return true;
        }
        restore_interrupts(saved_flags);
        *result = (u64) h;
        return true;
    }
    if (num == 72) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            *result = (u64) -1;
            return true;
        }
        handle* h = &g_handle_tables[caller_process][handle_idx];
        if (!h->used) {
            *result = (u64) -1;
            return true;
        }
        kernel_object* obj = &g_objects[h->object_index];
        if (obj->type != OBJ_THREAD) {
            *result = (u64) -1;
            return true;
        }
        thread_join(obj->data_index);
        *result = 0;
        return true;
    }
    if (num == 73) {
        // Deliberately narrower than syscall 12 (process_exit): no
        // free_address_space, no handle-table teardown - other threads
        // (or the main thread) may still be using both. A process exiting
        // while a spawned thread is still alive is a stated, out-of-scope
        // limitation for this first pass, same category as the already-
        // tracked stuck-respawn bug.
        // Same disable_interrupts() protection as the block above (and
        // syscall 12's own exit path) - used=false is shared scheduler
        // state yield()'s round-robin scan reads.
        u64 saved_flags = disable_interrupts();
        g_tasks[g_current_task].used = false;
        restore_interrupts(saved_flags);
        yield();
        *result = 0;  // never actually reached
        return true;
    }
    return false;
}
