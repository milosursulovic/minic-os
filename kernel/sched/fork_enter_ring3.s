# Real copy-on-write fork()'s child entry (Faza I point 4, item 8) -
# same "fabricate an iretq frame and jump into it" trick
# kernel/syscall/usermode.s's run_ring3_test already uses to bootstrap a
# BRAND NEW task into ring3, but restoring a FULL captured general-
# purpose register snapshot instead of just (entry, stack) - the child
# must resume at the exact rip/rsp the parent was at when it called
# fork(), with every register matching except rax (forced to 0, the
# real "you are the child" fork() return value). A new, standalone file
# - does not touch usermode.s or switch.s.
#
# void fork_enter_ring3(u64* regs) - never returns.
#   rdi = pointer to a 20-u64 array, this file's OWN layout (only this
#   file and kernel/syscall/handlers/fork.c's C-side writer need to
#   agree on it - unlike interrupts.s's real trap-frame push order,
#   which fork.c reads separately and re-packs into exactly this shape):
#     [0]=rax [1]=rbx [2]=rcx [3]=rdx [4]=rsi [5]=rdi [6]=rbp
#     [7]=r8  [8]=r9  [9]=r10 [10]=r11 [11]=r12 [12]=r13 [13]=r14 [14]=r15
#     [15]=rip [16]=cs [17]=rflags [18]=rsp [19]=ss

.intel_syntax noprefix
.code64

.global fork_enter_ring3

fork_enter_ring3:
    mov rbx, rdi        # keep the snapshot pointer in rbx - rdi itself
                         # gets overwritten by regs[5] below, and rbx is
                         # restored dead last for exactly this reason

    mov ax, 0x23         # user data selector (GDT index 4 | RPL 3)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    # ss isn't reloaded here - iretq below sets it from the pushed frame

    push qword ptr [rbx+19*8]   # ss
    push qword ptr [rbx+18*8]   # rsp
    push qword ptr [rbx+17*8]   # rflags
    push qword ptr [rbx+16*8]   # cs
    push qword ptr [rbx+15*8]   # rip

    mov rax, [rbx+0*8]
    mov rcx, [rbx+2*8]
    mov rdx, [rbx+3*8]
    mov rsi, [rbx+4*8]
    mov rdi, [rbx+5*8]
    mov rbp, [rbx+6*8]
    mov r8,  [rbx+7*8]
    mov r9,  [rbx+8*8]
    mov r10, [rbx+9*8]
    mov r11, [rbx+10*8]
    mov r12, [rbx+11*8]
    mov r13, [rbx+12*8]
    mov r14, [rbx+13*8]
    mov r15, [rbx+14*8]
    mov rbx, [rbx+1*8]   # rbx restored last - we were using it as our own base pointer
    iretq
