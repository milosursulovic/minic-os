// A real loader on top of per-process address spaces and the ring3
// mechanism. spawn_process() is a genuine loader: it treats a byte range
// as an opaque blob (no assumption it was compiled by this kernel),
// copies it into a freshly cloned private address space, and schedules
// a real task whose first and only act is entering ring3 at the loaded
// address - reusing run_ring3_test() unchanged, just called from inside
// the scheduler this time instead of a one-shot shell command.

#include "process.h"
#include "../mm/frames.h"
#include "../mm/paging.h"
#include "../sched/task.h"
#include "object.h"
#include "../disk/vfs.h"

#pragma GCC visibility push(hidden)
extern void run_ring3_test(u64 entry, u64 user_stack);
extern u8 g_test_prog_start;
extern u8 g_test_prog_end;
#pragma GCC visibility pop

process g_processes[4];
int g_process_count;

// The kernel-side "entry point" every loaded process's task starts at -
// looked up via g_current_task exactly like proc_a_entry/proc_b_entry
// already do, so one shared trampoline works for every loaded process
// without needing per-task function pointers. run_ring3_test() never
// returns - this is genuinely the last kernel-mode code this task ever
// runs; from here on it's ring3 only, preemptible by the timer like any
// other task.
void process_entry_trampoline(void) {
    task* self = &g_tasks[g_current_task];
    run_ring3_test(self->ring3_entry_vaddr, self->ring3_user_stack_top);
}

// Loads [image_start, image_end) into a brand-new private address space at
// load_vaddr (page-granular, rounded up), maps a one-page user stack at
// stack_vaddr, and schedules a real task that jumps straight into ring3
// at load_vaddr. Returns the new process's index into g_processes, or -1
// on failure (out of frames, out of task slots, or out of process slots).
int spawn_process(u8* image_start, u8* image_end, u64 load_vaddr, u64 stack_vaddr) {
    if (g_process_count >= 4) {
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
        // Every frame alloc_frame() hands out lives inside the flat 1GB
        // identity map, so write the image bytes straight through the
        // frame's own physical address, no need to switch into cr3 first.
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
    // PAGE_NX - the classic stack-hardening win (injected "shellcode" on
    // the stack can no longer be jumped to and executed). Deliberately
    // NOT applied to the image mapping above: this loader still flattens
    // a whole program (code + rodata/data/bss) into one contiguous
    // copyable blob with no tracked code/data boundary - marking the
    // WHOLE image NX would block the process's own legitimate code too.
    if (!map_page_in(cr3, stack_vaddr, (u64) stack_frame, 0x06 | PAGE_NX)) {  // writable + user, non-executable
        free_frame(stack_frame);
        return -1;
    }

    int task_index = g_task_count;
    if (!create_task_with_cr3(&process_entry_trampoline, cr3)) {
        return -1;
    }
    g_tasks[task_index].ring3_entry_vaddr = load_vaddr;
    g_tasks[task_index].ring3_user_stack_top = stack_vaddr + 4096;

    int proc_index = g_process_count;
    g_processes[proc_index].used = true;
    g_processes[proc_index].cr3 = cr3;
    g_processes[proc_index].task_index = task_index;
    g_process_count = g_process_count + 1;
    g_tasks[task_index].process_index = proc_index;

    // Every process gets a handle to itself for free, in the one well-
    // known slot ("handle 0 = myself") - its own handle table starts
    // completely empty, so the very first allocation into it is
    // guaranteed to land in slot 0.
    int self_object = alloc_object(OBJ_PROCESS, proc_index);
    alloc_handle(proc_index, self_object, RIGHT_QUERY);

    return proc_index;
}

// "The shell launches a program" is genuinely real - the image bytes
// come from a real file, read through the VFS, not a pointer range into
// the kernel's own compiled-in image. Everything past that is
// spawn_process() completely unchanged.
//
// 16KB buffer: generous headroom past this program's compiled size
// (real room, not just exactly enough).
static u8 g_loaded_image_buf[16384];

int spawn_process_from_path(const char* path, u64 load_vaddr, u64 stack_vaddr) {
    int n = vfs_read(path, &g_loaded_image_buf[0], 16384);
    if (n < 0) {
        return -1;
    }
    return spawn_process(&g_loaded_image_buf[0], &g_loaded_image_buf[(u32) n], load_vaddr, stack_vaddr);
}
