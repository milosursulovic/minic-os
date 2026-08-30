// Process loader on top of per-process address spaces + the ring3 entry
// mechanism.

#include "process.h"
#include "../kernel/mm/frames/frames.h"
#include "../kernel/mm/paging/paging.h"
#include "../kernel/sched/task.h"
#include "ipc/object/object.h"
#include "../kernel/fs/vfs/vfs.h"

#pragma GCC visibility push(hidden)
extern void run_ring3_test(u64 entry, u64 user_stack);
extern u8 g_test_prog_start;
extern u8 g_test_prog_end;
#pragma GCC visibility pop

process g_processes[MAX_PROCESSES];
int g_process_count;

// run_ring3_test() never returns - last kernel-mode code this task runs.
void process_entry_trampoline(void) {
    task* self = &g_tasks[g_current_task];
    run_ring3_test(self->ring3_entry_vaddr, self->ring3_user_stack_top);
}

// Loads [image_start, image_end) into a fresh address space, maps a
// user stack, schedules a task entering ring3 at load_vaddr. Returns
// the process index, or -1 on failure. Reuses an exited process slot
// if one exists, else appends (bounded by 4).
int spawn_process(u8* image_start, u8* image_end, u64 load_vaddr, u64 stack_vaddr) {
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

    int task_index = create_task_with_cr3(&process_entry_trampoline, cr3);
    if (task_index < 0) {
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
    g_tasks[task_index].process_index = proc_index;

    // handle 0 = myself, free for every process. (A reused slot's handle
    // table is already clean - process_exit() clears it at exit time.)
    int self_object = alloc_object(OBJ_PROCESS, proc_index);
    alloc_handle(proc_index, self_object, RIGHT_QUERY);

    return proc_index;
}

static u8 g_loaded_image_buf[16384];

int spawn_process_from_path(const char* path, u64 load_vaddr, u64 stack_vaddr) {
    int n = vfs_read(path, &g_loaded_image_buf[0], 16384);
    if (n < 0) {
        return -1;
    }
    return spawn_process(&g_loaded_image_buf[0], &g_loaded_image_buf[(u32) n], load_vaddr, stack_vaddr);
}
