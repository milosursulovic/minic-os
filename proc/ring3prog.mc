// Milestone 21: the first ring3 "program" this kernel runs that's
// actually compiled by minicc, not hand-assembled (proc/testprog.s,
// which this file replaces, was hand-written machine code specifically
// because nothing had proven a *compiled* MiniC program could survive
// this loader's "copy one contiguous byte range, jump to its first
// byte" model - see proc/ring3.ld and build.sh for the standalone
// link+objcopy step that makes that true here). Reproduces testprog.s's
// exact old behavior (two print syscalls with an embedded message, a
// self-handle query and an invalid-handle query, each printed) so this
// milestone has a clean, byte-for-byte comparable verification target.
//
// _start MUST be the first function declared in this file: ring3.ld's
// .text starts at address 0 with no reordering, and this codegen emits
// function bodies into .text in program-declaration order (no
// optimization pass ever reorders them - see codegen.cpp's `generate()`
// driver loop), so declaring _start first is what guarantees it's the
// very first byte of the flattened blob spawnProcess() jumps to.
// Verified once by hand via `objdump -d -M i386 ring3prog_linked.elf`
// before this got wired into the real build (see build.sh's comment).
void _start() {
    doSyscall(1, (u64) "hello from a LOADED process! 0x", 0xC0FFEE);
    doSyscall(1, (u64) "hello from a LOADED process! 0x", 0xC0FFEE);

    // handle 0 = myself (guaranteed by spawnProcess()) - should resolve
    // to this process's own taskIndex, checkable against `ps`'s output,
    // exactly like testprog.s's version did.
    u64 selfTaskIndex = doSyscall(3, 0, 0);
    doSyscall(1, (u64) "handle 0 (self) -> taskIndex 0x", selfTaskIndex);

    // handle 99 was never allocated - should come back as the -1
    // sentinel (0xffffffffffffffff), not garbage and not a crash.
    u64 badResult = doSyscall(3, 99, 0);
    doSyscall(1, (u64) "handle 99 (invalid) -> 0x", badResult);

    while (true) {
    }
}

// Syscall argument staging: asm(...) has no operand binding (nothing is
// ever live in a register across a statement boundary in this codegen -
// see compiler/CLAUDE.md), so a function can't just receive a param in
// rdi/rsi and immediately asm("int 0x80") - by the time an asm statement
// runs, whatever was in those registers has already been spilled to a
// stack slot. Same fix every other asm() block in this kernel already
// uses for globals: stage into a global first, then have the asm block
// load [rip+global] into the real register right before the instruction
// that needs it, and read the result back from another global right
// after.
u64 gSyscallNum;
u64 gArg0;
u64 gArg1;
u64 gSyscallResult;

u64 doSyscall(u64 num, u64 arg1, u64 arg2) {
    gSyscallNum = num;
    gArg0 = arg1;
    gArg1 = arg2;
    asm("mov rax, [rip+gSyscallNum]\nmov rdi, [rip+gArg0]\nmov rsi, [rip+gArg1]\nint 0x80\nmov [rip+gSyscallResult], rax");
    return gSyscallResult;
}
