# Interrupt entry stubs. Like boot.s, this is hand-written - saving/
# restoring full register state around an interrupt, and normalizing
# "sometimes the CPU pushes an error code, sometimes it doesn't" into one
# common call, is calling-convention plumbing below what a MiniC function
# body (or asm("...")'s no-operand-binding splice) can express.
#
# Handles: divide-by-zero (0), general protection fault (13), page fault
# (14) - just enough exceptions to report something useful if a kernel bug
# trips one - plus IRQ0/IRQ1 (timer/keyboard, remapped to vectors 32/33 by
# kmain.mc's PIC setup). Not the full 0-31 exception table yet; the same
# isr_noerr/isr_err macro pattern extends to the rest when something
# actually needs them.

.intel_syntax noprefix
.code64

.extern interrupt_handler
.extern syscall_dispatch
.global isr0
.global isr13
.global isr14
.global irq0
.global irq1
.global irq12
.global isr_syscall

.macro isr_noerr num
isr\num:
    push 0
    push \num
    jmp isr_common_stub
.endm

.macro isr_err num
isr\num:
    push \num
    jmp isr_common_stub
.endm

isr_noerr 0
isr_err   13
isr_err   14

irq0:
    push 0
    push 32
    jmp isr_common_stub

irq1:
    push 0
    push 33
    jmp isr_common_stub

irq12:
    push 0
    push 44
    jmp isr_common_stub

# Stack on entry: [vector, error_code, RIP, CS, RFLAGS, RSP, SS] (CPU pushed
# the last five; the stub above pushed the first two).
#
# Real bug fixed here (self-ptr-0x20 / stuck-respawn Part 2, ROOT CAUSE -
# found via QEMU record/replay + stepi, 2026-09-12): this used to read
# vector/error_code/RIP into rdi/rsi/rdx *before* pushing the interrupted
# code's own register state - clobbering whatever the interrupted code
# actually had in rdi/rsi/rdx at that exact instant, since the `push
# rdi`/`push rsi`/`push rdx` below then saved these OVERWRITTEN values
# instead of the originals. A timer IRQ (vector 32 = 0x20) landing between
# a `mov %rax,%rdi` and the following `call` - completely ordinary,
# unpreventable timing, not any kind of race - would silently replace the
# interrupted code's live `rdi` with the literal vector number, restored
# via `pop rdi` once the ISR returned. This is exactly why the corrupted
# value was always small (a real IDT vector number: 0x20 for the timer,
# matching this kernel's own IRQ0 remap) and why it could show up in
# *any* of rdi/rsi/rdx depending on which one the interrupted code
# happened to be using at that instant (self, a mouse_x/y pointer, RIP
# itself via rdx feeding a later jump/return) - not a scheduler race, not
# ASLR, not stack corruption; a plain register-clobber in the ISR entry
# stub itself. Push everything FIRST (preserving the original live
# registers exactly, same as `isr_syscall` below already correctly
# does), then read vector/error_code/RIP from their known stack slots
# *below* the 15 just-pushed registers (offset 15*8=120 onward) for
# interrupt_handler's own arguments - this is the only correctness-
# critical ordering; the ISR path is not itself timing-sensitive.
isr_common_stub:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, [rsp + 120]
    mov rsi, [rsp + 128]
    mov rdx, [rsp + 136]

    call interrupt_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16          # drop vector + error_code
    iretq

# Milestone 11's syscall gate (vector 0x80, DPL=3 so ring3's `int 0x80` is
# allowed to reach it at all - see drivers/interrupts_init.mc). Separate
# from isr_common_stub above: that one calls interrupt_handler(vector,
# error_code) - fine for hardware interrupts/exceptions, but a syscall
# needs its *arguments* (already sitting in rax/rdi/rsi/rdx when this
# fires, by this kernel's own calling convention - see syscall/syscall.mc)
# passed through, and its *return value* written back into the saved rax
# slot so `iretq` hands it to ring3 in rax, the way a function call would.
#
# No error code to drop here either - `int n` (a software interrupt,
# unlike a CPU-raised exception) never pushes one, for any vector.
isr_syscall:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    # Read the caller's syscall number/args from where they landed on the
    # stack (not straight from the registers - by now rdi/rsi/rdx/rcx are
    # about to be overwritten as this call's *own* arguments) and shuffle
    # them into SysV argument order for syscall_dispatch(num, a1, a2, a3).
    mov rcx, [rsp + 88]    # a3 = orig rdx
    mov rdx, [rsp + 80]    # a2 = orig rsi
    mov rsi, [rsp + 72]    # a1 = orig rdi
    mov rdi, [rsp + 112]   # num = orig rax

    call syscall_dispatch

    mov [rsp + 112], rax   # overwrite the saved rax with the return value

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    iretq
