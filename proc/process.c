// Process loader on top of per-process address spaces + the ring3 entry
// mechanism.

#include "process.h"
#include "../kernel/mm/frames/frames.h"
#include "../kernel/mm/paging/paging.h"
#include "../kernel/sched/task.h"
#include "ipc/object/object.h"
#include "../kernel/fs/vfs/vfs.h"
#include "../kernel/security/exec_sign/exec_sign.h"
#include "../kernel/security/sandbox/sandbox.h"

#pragma GCC visibility push(hidden)
extern void run_ring3_test(u64 entry, u64 user_stack);
extern u8 g_test_prog_start;
extern u8 g_test_prog_end;
#pragma GCC visibility pop

process g_processes[MAX_PROCESSES];
int g_process_count;

// Save/restore IF - same established pattern kernel/mm/frames/frames.c's
// alloc_frame()/kernel/mm/heap/heap.c's kalloc() use. Real stuck-respawn
// bug (found 2026-08-30, root-caused here): create_task_with_cr3() sets
// the new task's used=true immediately, making it schedulable, but
// spawn_process() below still has several steps left to finish wiring
// it up (ring3_entry_vaddr/stack, g_processes[proc_index], and the
// task's own process_index back-link). A timer-ISR preemption landing
// in that window could switch to the brand-new task while its
// process_index was still -1; for a program that exits almost
// instantly (hello_service.c is exactly process_exit() then an
// unreachable loop), the child's process_exit() syscall would then read
// process_index=-1, skip marking the process used=false, and just kill
// the task - orphaning the process record as used=true forever, since
// no task would ever call exit for that proc_index again. Disabling
// interrupts across the whole registration sequence closes the window.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

// run_ring3_test() never returns - last kernel-mode code this task runs.
void process_entry_trampoline(void) {
    task* self = &g_tasks[g_current_task];
    run_ring3_test(self->ring3_entry_vaddr, self->ring3_user_stack_top);
}

// Loads [image_start, image_end) into a fresh address space, maps a
// user stack, schedules a task entering ring3 at load_vaddr. Returns
// the process index, or -1 on failure. Reuses an exited process slot
// if one exists, else appends (bounded by 4).
int spawn_process(u8* image_start, u8* image_end, u64 load_vaddr, u64 stack_vaddr, bool sandboxed) {
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
        return -1;
    }

    u64 cr3 = clone_address_space();
    if (cr3 == 0) {
        return -1;
    }

    u64 image_size = (u64) image_end - (u64) image_start;
    u64 page_count = (image_size + 4095) / 4096;
    u64 copied = 0;
    u64 page_index = 0;
    while (page_index < page_count) {
        void* frame = alloc_frame();
        if (frame == NULL) {
            return -1;
        }
        if (!map_page_in(cr3, load_vaddr + (page_index * 4096), (u64) frame, 0x06)) {  // writable + user
            free_frame(frame);
            return -1;
        }
        // frame's address is identity-mapped, so write straight through it.
        u8* dst = (u8*) frame;
        u32 i = 0;
        while (i < 4096 && copied < image_size) {
            dst[i] = image_start[copied];
            copied = copied + 1;
            i = i + 1;
        }
        page_index = page_index + 1;
    }

    void* stack_frame = alloc_frame();
    if (stack_frame == NULL) {
        return -1;
    }
    // NX on the stack only - the image itself has no code/data split to mark NX.
    if (!map_page_in(cr3, stack_vaddr, (u64) stack_frame, 0x06 | PAGE_NX)) {
        free_frame(stack_frame);
        return -1;
    }

    // Critical section: from the moment the task exists (and is therefore
    // schedulable - see this file's own disable_interrupts() comment
    // above) until it's fully wired up. Without this, a timer-ISR
    // preemption in the middle could run the brand-new task before its
    // process_index back-link is set.
    u64 saved_flags = disable_interrupts();

    int task_index = create_task_with_cr3(&process_entry_trampoline, cr3);
    if (task_index < 0) {
        restore_interrupts(saved_flags);
        return -1;
    }
    g_tasks[task_index].ring3_entry_vaddr = load_vaddr;
    g_tasks[task_index].ring3_user_stack_top = stack_vaddr + 4096;

    if (proc_index < 0) {
        proc_index = g_process_count;
        g_process_count = g_process_count + 1;
    }
    g_processes[proc_index].used = true;
    g_processes[proc_index].cr3 = cr3;
    g_processes[proc_index].task_index = task_index;
    g_processes[proc_index].uid = 0;
    // 64KB past the main thread's own one-page stack - generous, collision-
    // free room for thread_create() (syscall 71) to hand out real, distinct
    // stack pages without any per-thread region bookkeeping.
    g_processes[proc_index].main_stack_vaddr = stack_vaddr;
    g_processes[proc_index].next_thread_stack_vaddr = stack_vaddr + 0x10000;
    g_tasks[task_index].process_index = proc_index;

    // handle 0 = myself, free for every process. (A reused slot's handle
    // table is already clean - process_exit() clears it at exit time.)
    int self_object = alloc_object(OBJ_PROCESS, proc_index);
    alloc_handle(proc_index, self_object, RIGHT_QUERY);

    // Faza I point 14, item 17: granted here, inside the same protected
    // section as the self-handle above, not after this function returns -
    // otherwise the brand-new task (already schedulable the instant
    // create_task_with_cr3() ran) could get preempted into and make a
    // syscall before the restriction actually existed.
    if (sandboxed) {
        int policy_slot = sandbox_policy_create(sandbox_default_denied_low(), sandbox_default_denied_high());
        if (policy_slot >= 0) {
            int sandbox_object = alloc_object(OBJ_SANDBOX, policy_slot);
            if (sandbox_object >= 0) {
                alloc_handle(proc_index, sandbox_object, 0);
            }
        }
    }

    restore_interrupts(saved_flags);
    return proc_index;
}

// Real, latent capacity bug found 2026-08-31 (same "bump the cap before
// the new consumer starves everyone else" class as MAX_TASKS/
// MAX_PROCESSES earlier this session): ring3prog.c's own natural growth
// this session (Thread/Event/Mutex/Timer/SharedMemory-sync demos) pushed
// its compiled size (0x7b00 = 31488 bytes as of this fix) past the old
// 16384-byte cap - fs_read_file() correctly refuses (`size > max_len`
// returns -2, not a silent truncation), so spawn_process_from_path()
// simply always failed for testprog.bin from that point on, with no
// crash - just spawn_process()-family syscalls silently returning -1.
// 65536 is real headroom, not another exact-fit.
#define LOADED_IMAGE_BUF_SIZE 65536
static u8 g_loaded_image_buf[LOADED_IMAGE_BUF_SIZE];

// Faza I point 14, item 17: this is a real trust boundary - path can
// point anywhere writable (/system, /apps, /fat32), so unlike every
// direct spawn_process() call site above (all trusted builtins baked
// into kernel.elf at link time), whatever this reads must carry a valid
// kernel/security/exec_sign/ signature before a single byte of it runs.
// A verified spawn is also automatically sandboxed (sandboxed=true) -
// least-privilege by default for anything loaded from disk, no opt-out.
int spawn_process_from_path(const char* path, u64 load_vaddr, u64 stack_vaddr) {
    int n = vfs_read(path, &g_loaded_image_buf[0], LOADED_IMAGE_BUF_SIZE, 0);  // out of item 7's scope - unchanged, fully-permissive spawn read
    if (n < 0) {
        return -1;
    }
    const u8* payload;
    u32 payload_len;
    if (!exec_sign_verify(&g_loaded_image_buf[0], (u32) n, &payload, &payload_len)) {
        return -1;
    }
    return spawn_process((u8*) payload, (u8*) payload + payload_len, load_vaddr, stack_vaddr, true);
}
