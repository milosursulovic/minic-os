// Milestone 13: a real loader on top of milestone 12's per-process
// address spaces and milestone 11's ring3 mechanism - the two things
// README's Known limitations flagged as "not wired together yet."
//
// Before this, "isolated tasks" (proc_a_entry/proc_b_entry) were just
// ordinary MiniC functions already linked into the kernel image, run in
// RING 0 with a different CR3 - proving address-space isolation, but not
// a loader (nothing was ever *loaded*) and not ring3 (nothing ran
// unprivileged). spawn_process() below is a genuine loader: it treats a
// byte range as an opaque blob (no assumption it was compiled by this
// kernel, or even by minicc at all - proc/testprog.s is hand-assembled
// machine code, not MiniC), copies it into a freshly cloned private
// address space, and schedules a real task whose first and only act is
// entering ring3 at the loaded address - reusing milestone 11's
// run_ring3_test unchanged, just called from inside the scheduler this
// time instead of a one-shot cli-wrapped shell command.

import "../mm/frames.mc";
import "../mm/paging.mc";
import "../sched/task.mc";
import "../syscall/syscall.mc";
import "object.mc";
import "../disk/vfs.mc";

extern u8 g_test_prog_start;
extern u8 g_test_prog_end;

struct process {
    bool used;
    u64 cr3;
    int task_index;
}

process g_processes[4];
int g_process_count;

// The kernel-side "entry point" every loaded process's task starts at -
// looked up via g_current_task exactly like proc_a_entry/proc_b_entry already
// do, so one shared trampoline works for every loaded process without
// needing per-task function pointers. run_ring3_test() never returns -
// this is genuinely the last kernel-mode code this task ever runs; from
// here on it's ring3 only, preemptible by the timer like any other task.
void process_entry_trampoline() {
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
        if (frame == null) {
            return -1;
        }
        if (!map_page_in(cr3, load_vaddr + (page_index * 4096), (u64) frame, 0x06)) {   // writable + user
            free_frame(frame);
            return -1;
        }
        // Every frame alloc_frame() hands out lives inside the flat 1GB
        // identity map (same reasoning map_page_in's own comment gives for
        // walking page tables directly) - write the image bytes straight
        // through the frame's own physical address, no need to switch
        // into cr3 first.
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
    if (stack_frame == null) {
        return -1;
    }
    // Milestone 28: PAGE_NX - the classic stack-hardening win (injected
    // "shellcode" on the stack can no longer be jumped to and executed).
    // Deliberately NOT applied to the image mapping above: this loader
    // still flattens a whole program (code + rodata/data/bss) into one
    // contiguous copyable blob with no tracked code/data boundary (see
    // ring3.ld/build.sh) - marking the WHOLE image NX would block the
    // process's own legitimate code too. A real W^X split within a
    // loaded image is a separate, larger problem, not tackled here.
    if (!map_page_in(cr3, stack_vaddr, (u64) stack_frame, 0x06 | page_nx)) {   // writable + user, non-executable
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
    // guaranteed to land in slot 0. Ring3 code can rely on that without
    // needing a syscall just to discover its own handle.
    int self_object = alloc_object(obj_process, proc_index);
    alloc_handle(proc_index, self_object, right_query);

    return proc_index;
}

// Milestone 19: "the shell launches a program" becomes genuinely real -
// the image bytes come from a real file, read through the VFS (so
// either backend could in principle supply them, though only MiniFS
// makes sense for something you'd actually execute), not a pointer
// range into the kernel's own compiled-in image. Everything past that
// is spawn_process() completely unchanged: loading from disk is only
// ever "get the bytes into RAM," never a second loader.
//
// Milestone 24 hit the exact "anything bigger" case this comment always
// warned about: proc/ring3prog.mc grew past 4096 bytes once it gained
// real File/Channel/Process/POSIX-shim code, and spawn_process_from_path()
// silently failed (fs_read_file's own milestone-19 "too large for the
// caller's buffer" sentinel, indistinguishable at this call site from
// any other failure) the moment the compiled program crossed that
// line. Bumped to 16384 (16KB) - generous headroom for this program's
// current size (~8KB) and near-future growth, the same "give it real
// room, not just exactly enough" reasoning already applied to
// stack_vaddr's own milestone-24 fix.
u8 g_loaded_image_buf[16384];

int spawn_process_from_path(char* path, u64 load_vaddr, u64 stack_vaddr) {
    int n = vfs_read(path, &g_loaded_image_buf[0], 16384);
    if (n < 0) {
        return -1;
    }
    return spawn_process(&g_loaded_image_buf[0], &g_loaded_image_buf[(u32) n], load_vaddr, stack_vaddr);
}
