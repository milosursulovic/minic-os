# Milestone 11's ring3 entry: builds a fake `iretq` frame (same trick
# interrupts.s already relies on for returning FROM a ring3 interrupt,
# just used here to enter ring3 for the first time instead) and jumps
# into it. Below what asm("...") can express for the usual reason - a
# privilege-level transition is program structure, not a value a MiniC
# statement could carry.
#
# void run_ring3_test(u64 entry, u64 user_stack)
#   rdi = ring3 entry point
#   rsi = ring3 stack pointer (top of a page already mapped present +
#         writable + *user*, or this faults immediately on the first push)
#
# Never returns - there's no ring3 "exit" (no process-teardown mechanism
# exists yet, a still-open gap - see README's Known limitations). The
# calling task's kernel-mode life ends here; from this point on it only
# ever runs in ring3, preemptible by the timer like any other task (see
# proc/process.mc's process_entry_trampoline(), the only caller since
# milestone 13 retired the one-shot demo that used to call this too).

.intel_syntax noprefix
.code64

.global run_ring3_test

run_ring3_test:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov ax, 0x23        # user data selector (GDT index 4 | RPL 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    # ss isn't reloaded here - iretq below sets it from the pushed frame

    push 0x23            # SS
    push rsi              # RSP
    pushfq                 # RFLAGS (IF is already 1 - we're long past boot's `sti`)
    push 0x1B             # CS (GDT index 3 | RPL 3)
    push rdi               # RIP (entry point)
    iretq
