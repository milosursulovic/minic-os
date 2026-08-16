# A tiny, hand-assembled ring3 "program" - milestone 13's loader target.
# Deliberately NOT a MiniC function compiled into the kernel image (that
# would just be milestone 11/12's demo trick again - kernel code with a
# borrowed address space, not a real loader). The kernel treats this as
# an opaque byte blob: spawnProcess() (proc/process.mc) copies it into a
# freshly mapped, freshly cloned private address space and jumps into it
# via the same fake-iretq-frame trick usermode.s already uses to enter
# ring3.
#
# Position-independent by construction: every address it touches is
# [rip+label], never an absolute `offset label`. This matters because
# the blob gets COPIED to a virtual address (0x80000000, milestone 12's
# private-region base) that has nothing to do with wherever it happens
# to link inside this kernel image - an absolute address baked in at
# link time would be wrong the instant it's copied anywhere else.
# RIP-relative offsets survive the copy unchanged, since the whole blob
# moves as one contiguous unit and the *relative* distance between an
# instruction and gTestProgMsg never changes.
#
# What it does: two `int 0x80` syscalls (number 1 = print) with a
# message living inside the blob itself - proving these are genuinely
# the loaded bytes executing at the loaded address under the process's
# own private address space, not a kernel string or a kernel function
# pretending - then spins forever, preemptible by the timer exactly like
# any other task (task3Entry's whole reason for existing, just proven
# here for a ring3 context for the first time).

.intel_syntax noprefix
.code64

.global gTestProgStart
.global gTestProgEnd

gTestProgStart:
    lea rdi, [rip + gTestProgMsg]
    mov rsi, 0xC0FFEE
    mov rax, 1
    int 0x80

    lea rdi, [rip + gTestProgMsg]
    mov rsi, 0xC0FFEE
    mov rax, 1
    int 0x80

spin:
    jmp spin

gTestProgMsg:
    .asciz "hello from a LOADED process! 0x"

gTestProgEnd:
