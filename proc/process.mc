// Milestone 13: a real loader on top of milestone 12's per-process
// address spaces and milestone 11's ring3 mechanism - the two things
// README's Known limitations flagged as "not wired together yet."
//
// Before this, "isolated tasks" (procAEntry/procBEntry) were just
// ordinary MiniC functions already linked into the kernel image, run in
// RING 0 with a different CR3 - proving address-space isolation, but not
// a loader (nothing was ever *loaded*) and not ring3 (nothing ran
// unprivileged). spawnProcess() below is a genuine loader: it treats a
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

extern u8 gTestProgStart;
extern u8 gTestProgEnd;

struct Process {
    bool used;
    u64 cr3;
    int taskIndex;
}

Process gProcesses[4];
int gProcessCount;

// The kernel-side "entry point" every loaded process's task starts at -
// looked up via gCurrentTask exactly like procAEntry/procBEntry already
// do, so one shared trampoline works for every loaded process without
// needing per-task function pointers. run_ring3_test() never returns -
// this is genuinely the last kernel-mode code this task ever runs; from
// here on it's ring3 only, preemptible by the timer like any other task.
void processEntryTrampoline() {
    Task* self = &gTasks[gCurrentTask];
    run_ring3_test(self->ring3EntryVaddr, self->ring3UserStackTop);
}

// Loads [imageStart, imageEnd) into a brand-new private address space at
// loadVaddr (page-granular, rounded up), maps a one-page user stack at
// stackVaddr, and schedules a real task that jumps straight into ring3
// at loadVaddr. Returns the new process's index into gProcesses, or -1
// on failure (out of frames, out of task slots, or out of process slots).
int spawnProcess(u8* imageStart, u8* imageEnd, u64 loadVaddr, u64 stackVaddr) {
    if (gProcessCount >= 4) {
        return -1;
    }

    u64 cr3 = cloneAddressSpace();
    if (cr3 == 0) {
        return -1;
    }

    u64 imageSize = (u64) imageEnd - (u64) imageStart;
    u64 pageCount = (imageSize + 4095) / 4096;
    u64 copied = 0;
    u64 pageIndex = 0;
    while (pageIndex < pageCount) {
        void* frame = allocFrame();
        if (frame == null) {
            return -1;
        }
        if (!mapPageIn(cr3, loadVaddr + (pageIndex * 4096), (u64) frame, 0x06)) {   // writable + user
            freeFrame(frame);
            return -1;
        }
        // Every frame allocFrame() hands out lives inside the flat 1GB
        // identity map (same reasoning mapPageIn's own comment gives for
        // walking page tables directly) - write the image bytes straight
        // through the frame's own physical address, no need to switch
        // into cr3 first.
        u8* dst = (u8*) frame;
        u32 i = 0;
        while (i < 4096 && copied < imageSize) {
            dst[i] = imageStart[copied];
            copied = copied + 1;
            i = i + 1;
        }
        pageIndex = pageIndex + 1;
    }

    void* stackFrame = allocFrame();
    if (stackFrame == null) {
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
    if (!mapPageIn(cr3, stackVaddr, (u64) stackFrame, 0x06 | PAGE_NX)) {   // writable + user, non-executable
        freeFrame(stackFrame);
        return -1;
    }

    int taskIndex = gTaskCount;
    if (!createTaskWithCr3(&processEntryTrampoline, cr3)) {
        return -1;
    }
    gTasks[taskIndex].ring3EntryVaddr = loadVaddr;
    gTasks[taskIndex].ring3UserStackTop = stackVaddr + 4096;

    int procIndex = gProcessCount;
    gProcesses[procIndex].used = true;
    gProcesses[procIndex].cr3 = cr3;
    gProcesses[procIndex].taskIndex = taskIndex;
    gProcessCount = gProcessCount + 1;
    gTasks[taskIndex].processIndex = procIndex;

    // Every process gets a handle to itself for free, in the one well-
    // known slot ("handle 0 = myself") - its own handle table starts
    // completely empty, so the very first allocation into it is
    // guaranteed to land in slot 0. Ring3 code can rely on that without
    // needing a syscall just to discover its own handle.
    int selfObject = allocObject(OBJ_PROCESS, procIndex);
    allocHandle(procIndex, selfObject, RIGHT_QUERY);

    return procIndex;
}

// Milestone 19: "the shell launches a program" becomes genuinely real -
// the image bytes come from a real file, read through the VFS (so
// either backend could in principle supply them, though only MiniFS
// makes sense for something you'd actually execute), not a pointer
// range into the kernel's own compiled-in image. Everything past that
// is spawnProcess() completely unchanged: loading from disk is only
// ever "get the bytes into RAM," never a second loader.
//
// Milestone 24 hit the exact "anything bigger" case this comment always
// warned about: proc/ring3prog.mc grew past 4096 bytes once it gained
// real File/Channel/Process/POSIX-shim code, and spawnProcessFromPath()
// silently failed (fsReadFile's own milestone-19 "too large for the
// caller's buffer" sentinel, indistinguishable at this call site from
// any other failure) the moment the compiled program crossed that
// line. Bumped to 16384 (16KB) - generous headroom for this program's
// current size (~8KB) and near-future growth, the same "give it real
// room, not just exactly enough" reasoning already applied to
// stackVaddr's own milestone-24 fix.
u8 gLoadedImageBuf[16384];

int spawnProcessFromPath(char* path, u64 loadVaddr, u64 stackVaddr) {
    int n = vfsRead(path, &gLoadedImageBuf[0], 16384);
    if (n < 0) {
        return -1;
    }
    return spawnProcess(&gLoadedImageBuf[0], &gLoadedImageBuf[(u32) n], loadVaddr, stackVaddr);
}
