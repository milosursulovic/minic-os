# Context switch: saves the currently running task's callee-saved
# registers onto its own stack, stashes the resulting stack pointer,
# switches to the next task's stack, and restores ITS callee-saved
# registers - then `ret` "returns" into wherever that stack was left.
# For a task that's never run before, that's not a real previous switch -
# task.mc's createTask() hand-builds a stack that looks exactly like what
# this function would have left behind, with the task's entry point as
# the "return address", so the very first switch into it lands there via
# an ordinary `ret`.
#
# void switch_context(u64* oldRspOut, u64 newRsp)
#   rdi = where to save the current (about to be suspended) task's rsp
#   rsi = the rsp to switch to (the next task's saved stack pointer)
#
# Below what asm("...") can express for the same reason boot.s/
# interrupts.s are hand-written: MiniC's asm() has no operand binding and
# this compiler never keeps a value live in a register across a statement
# boundary - but a context switch's entire point is preserving register
# state *across* what looks like one ordinary call into a completely
# different task's stack.
#
# The `sti` right before `ret` (not a stray instruction - x86 guarantees
# the instruction immediately after `sti` executes before any interrupt
# can be delivered, so this can't race with the `ret` itself) is what
# makes milestone 9's preemption actually work rather than deadlock: this
# is the ONE place execution transfers to a task, whether it's resuming
# (mid-yield()) or running for the very first time via createTask()'s
# fake stack. Putting the re-enable in MiniC's yield() instead - "after
# switch_context returns" - only covers the resuming case; a chain of
# brand-new tasks that cascade through each other without ever returning
# (like task1 -> task2 -> task3 during task3's very first timer
# preemption, none of whose switch_context calls have returned yet) would
# leave interrupts globally off with nothing left to ever turn them back
# on - task3 doesn't yield, so it would spin forever with no timer ticks
# able to fire again. Found this the hard way: the kernel hung solid
# (100% host CPU, no further serial output) the first time preemption was
# wired up with `sti` in yield() instead of here.

.intel_syntax noprefix
.code64

.global switch_context
switch_context:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, rsi

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    sti
    ret
