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
# What it does: two `int 0x80` print syscalls (number 1) with a message
# living inside the blob itself - proving these are genuinely the loaded
# bytes executing at the loaded address under the process's own private
# address space, not a kernel string or a kernel function pretending -
# then (milestone 14) two handle-query syscalls (number 3): one against
# handle 0 (every process's well-known "handle to myself"), one against
# handle 99 (never allocated, deliberately invalid), printing whatever
# comes back from each so the serial log holds a real positive and a
# real negative result side by side - then spins forever, preemptible by
# the timer exactly like any other task.

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

    # handle 0 = myself (guaranteed by spawnProcess()) - should resolve
    # to this process's own taskIndex, checkable against `ps`'s output.
    mov rax, 3
    mov rdi, 0
    int 0x80
    mov rsi, rax
    lea rdi, [rip + gMsgSelfHandle]
    mov rax, 1
    int 0x80

    # handle 99 was never allocated - should come back as the -1
    # sentinel (0xffffffffffffffff), not garbage and not a crash.
    mov rax, 3
    mov rdi, 99
    int 0x80
    mov rsi, rax
    lea rdi, [rip + gMsgBadHandle]
    mov rax, 1
    int 0x80

spin:
    jmp spin

gTestProgMsg:
    .asciz "hello from a LOADED process! 0x"
gMsgSelfHandle:
    .asciz "handle 0 (self) -> taskIndex 0x"
gMsgBadHandle:
    .asciz "handle 99 (invalid) -> 0x"

gTestProgEnd:
