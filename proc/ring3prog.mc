// Milestone 21: the first ring3 "program" this kernel runs that's
// actually compiled by minicc, not hand-assembled (proc/testprog.s,
// which this file replaced, was hand-written machine code specifically
// because nothing had proven a *compiled* MiniC program could survive
// this loader's "copy one contiguous byte range, jump to its first
// byte" model - see proc/ring3.ld and build.sh for the standalone
// link+objcopy step that makes that true here).
//
// Milestone 22 extended it to exercise the new native "File" API (real
// `.write()`/`.read()` methods, milestone 20's syntax, wrapping the new
// vfsRead/vfsWrite syscalls, numbers 4/5) - the first time this kernel's
// method-call syntax is used for something real, not just a language
// demo.
//
// _start MUST be the first FUNCTION declared in this file: ring3.ld's
// .text starts at address 0 with no reordering, and this codegen emits
// function bodies into .text in program-declaration order (no
// optimization pass ever reorders them - see codegen.cpp's `generate()`
// driver loop), so declaring _start first is what guarantees it's the
// very first byte of the flattened blob spawnProcess() jumps to.
// Verified once by hand via `objdump -d -m i386:x86-64
// ring3prog_linked.elf` before this got wired into the real build (see
// build.sh's comment). `struct File` below it is fine - a struct
// declaration emits nothing to .text at all (`buildStructRegistry` runs
// as a separate pass before any function body is generated), it just
// has to appear *textually* before _start's body references the type
// (MiniC requires a struct to be declared before use even within one
// file - confirmed by testing, not assumed).
struct File {
    char* path;
}

void _start() {
    doSyscall(1, (u64) "hello from a LOADED process! 0x", 0xC0FFEE, 0);
    doSyscall(1, (u64) "hello from a LOADED process! 0x", 0xC0FFEE, 0);

    // handle 0 = myself (guaranteed by spawnProcess()) - should resolve
    // to this process's own taskIndex, checkable against `ps`'s output,
    // exactly like testprog.s's version did.
    u64 selfTaskIndex = doSyscall(3, 0, 0, 0);
    doSyscall(1, (u64) "handle 0 (self) -> taskIndex 0x", selfTaskIndex, 0);

    // handle 99 was never allocated - should come back as the -1
    // sentinel (0xffffffffffffffff), not garbage and not a crash.
    u64 badResult = doSyscall(3, 99, 0, 0);
    doSyscall(1, (u64) "handle 99 (invalid) -> 0x", badResult, 0);

    // Milestone 22: a real File object, real method calls
    // (msgFile.write(...)/msgFile.read(...)), from inside an actual
    // ring3 process - not the kernel calling vfsWrite/vfsRead directly
    // the way the shell's `vfswrite`/`vfscat` commands do.
    File msgFile;
    msgFile.path = "/system/ring3msg.txt";
    u64 written = msgFile.write("hello from ring3, via a real File.write() method call!", 54);
    doSyscall(1, (u64) "File.write() wrote 0x", written, 0);

    u64 readBack = msgFile.read((char*) &gReadBuf[0], 63);
    gReadBuf[readBack] = 0;
    doSyscall(1, (u64) "File.read() got back 0x", readBack, 0);
    doSyscall(1, (u64) &gReadBuf[0], 0, 0);

    while (true) {
    }
}

// Syscall argument staging: asm(...) has no operand binding (nothing is
// ever live in a register across a statement boundary in this codegen -
// see compiler/CLAUDE.md), so a function can't just receive a param in
// rdi/rsi/rdx and immediately asm("int 0x80") - by the time an asm
// statement runs, whatever was in those registers has already been
// spilled to a stack slot. Same fix every other asm() block in this
// kernel already uses for globals: stage into a global first, then have
// the asm block load [rip+global] into the real register right before
// the instruction that needs it, and read the result back from another
// global right after.
u64 gSyscallNum;
u64 gArg0;
u64 gArg1;
u64 gArg2;
u64 gSyscallResult;

u64 doSyscall(u64 num, u64 arg1, u64 arg2, u64 arg3) {
    gSyscallNum = num;
    gArg0 = arg1;
    gArg1 = arg2;
    gArg2 = arg3;
    asm("mov rax, [rip+gSyscallNum]\nmov rdi, [rip+gArg0]\nmov rsi, [rip+gArg1]\nmov rdx, [rip+gArg2]\nint 0x80\nmov [rip+gSyscallResult], rax");
    return gSyscallResult;
}

// The native "File" API: real methods (milestone 20 syntax) wrapping
// the raw vfsRead/vfsWrite syscalls (numbers 4/5, milestone 22) - the
// whole point of building method-call syntax before this. Deliberately
// minimal: the kernel's own vfsRead/vfsWrite are already whole-file,
// path-based, stateless operations (no open/close, no seek/position),
// so a File is just a path plus real methods, not a new kernel-side
// file-descriptor mechanism no other part of this kernel needs yet.
u64 File.write(File* self, char* buf, u64 len) {
    return doSyscall(5, (u64) self->path, (u64) buf, len);
}

u64 File.read(File* self, char* buf, u64 maxLen) {
    return doSyscall(4, (u64) self->path, (u64) buf, maxLen);
}

u8 gReadBuf[64];
