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
// Milestone 23 extended it further with `Channel`/`Process` - real
// `.receive()`/`.spawn()` methods wrapping two more new syscalls
// (numbers 7/6). Deliberately sequenced with a blocking
// `Channel.receive()` gating the `Process.spawn()` call, rather than
// spawning unconditionally at boot: this program is the SAME blob
// `Process.spawn()` below launches as a child, so an unconditional
// spawn would have every child spawn another child forever. Blocking
// on a channel that only the *shell* (via the new `ring3go` command,
// operator-triggered, after `install` has put the blob on disk) ever
// sends to solves this for free - the spawned child reaches its own
// `Channel.receive()` call too, but the channel's already empty again
// (single-slot mailbox, consumed by the parent), so it just blocks
// there forever instead of spawning a second generation. No taskIndex-
// checking or other recursion-guard hack needed.
//
// Milestone 24 added a thin POSIX-shaped shim - open/read/write/close,
// plain free functions (not methods, deliberately - the whole point is
// looking like the real POSIX API for anything ported against it, not
// extending the native OO-style API) implemented ENTIRELY in this
// ring3 program, with no new kernel syscalls or kernel changes at all.
// The native File API is whole-file only (no seek/position, no
// incremental read/write) - a real fd's read()/write() need to serve
// PARTIAL requests and track a cursor across multiple calls, so
// open() reads (or starts) the whole file into a per-fd in-memory
// buffer once, read()/write() serve out of that buffer while advancing
// a position, and close() flushes a written buffer back out through
// File.write() in one shot. A real, if small, gap from true POSIX:
// only one open-for-write "session" can safely target a given path (a
// second concurrent writer would each hold its own buffer and the last
// close() to run wins) - fine for this demo's single-fd-at-a-time use,
// a real note for anyone building on this later.
//
// This milestone also grew the compiled image past 4096 bytes (one
// page) for the first time - which exposed a real, separate bug in the
// LOADER's fixed constants, not this file: kmain.mc/shell.mc/this
// file's own Process.spawn() call all hardcoded loadVaddr=0x80000000/
// stackVaddr=0x80001000, which was only ever safe because every ring3
// program until now fit in exactly one page. Once this one needed a
// second page, the image's own second page (0x80000000+4096..) and
// the user stack (0x80001000..) landed on the SAME virtual address,
// and whichever mapPageIn() call ran second silently won, leaving the
// other one's backing memory unreachable at that address. Fixed by
// moving stackVaddr out to 0x80020000 (128KB of headroom) at all three
// call sites - see kmain.mc/shell.mc's own comments.
//
// Milestone 25 (Phase IX, capability/permission work): `Channel` no
// longer takes a raw channel index directly - `Channel.open()` (new)
// turns an index into a real, rights-checked handle via a new syscall
// (number 9), and `.send()`/`.receive()` now pass that handle instead.
// The kernel grants RIGHT_RECEIVE only from `open()`, never
// RIGHT_SEND - so this file's own `Channel.send()` call is now
// EXPECTED to fail, and is exercised for the first time specifically
// to prove that failure is real, not just untested.
//
// Milestone 26 added the `triggerValue == 2` branch below - a deliberate
// forbidden write into the shared kernel region, the negative-space
// proof for that milestone's `cloneAddressSpace()` fix. See mm/paging.mc
// for the kernel-side change; this file just needed a way to TRIGGER the
// attempt on command.
//
// Milestone 27 (Phase IX's second capability step): extends real
// per-handle rights to `Process` objects, the same way milestone 25 did
// for `Channel`. The gap here was narrower and more concrete than it
// first looked - `RIGHT_QUERY` already existed and was already granted
// to every process's own self-handle (milestone 14's `spawnProcess()`),
// but syscall 3 (query) never actually CHECKED it - any valid OBJ_PROCESS
// handle could query regardless of its rights bitmask. New syscall 10
// (`openProcess`) is the first real CROSS-process capability: given
// another task's index, it mints a handle to THAT process in the
// caller's own table, with the caller REQUESTING a rights bitmask that
// the kernel intersects against what's actually grantable
// (`requested & RIGHT_QUERY` - still the only real Process operation).
// `ProcessHandle` (new, below) wraps this - `.open(taskIndex, rights)`/
// `.query()`. Requesting 0 gives a handle that's real and valid but can
// do nothing, which is the actual proof exercised in `_start()`: open a
// handle to the just-spawned child with rights=0, show its `.query()` is
// rejected, then open a SECOND handle with RIGHT_QUERY and show that one
// succeeds - and returns the exact same taskIndex `Process.spawn()`
// already reported, a real cross-check like milestone 14's original
// handle-0-self-query proof.
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

struct Channel {
    int handle;
}

struct Process {
    char* path;
}

// Milestone 27: a handle-based counterpart to `Process` (which is
// path-based, spawn-only) - wraps syscalls 10/3, the same shape
// `Channel` wraps 9/7/8. Kept as a separate struct rather than adding
// fields to `Process` since the two represent genuinely different
// things: `Process.path` names a FILE to spawn from, `ProcessHandle`
// names an ALREADY-RUNNING task to query.
struct ProcessHandle {
    int handle;
}

// Mirrors object.mc's real ABI constant - duplicated here because this
// file is compiled standalone (--freestanding, no import of kernel
// internals) and only knows the syscall ABI, same reason doSyscall()'s
// numbers themselves are just written as literals.
const int RIGHT_QUERY = 1;

struct FileDescriptor {
    bool used;
    bool forWriting;
    char* path;
    u64 position;
    u64 length;
    u8 buffer[256];
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

    // Milestone 24: the POSIX shim, exercised with two separate write()
    // calls (proving the buffer genuinely accumulates across calls, not
    // just capturing one big write) and two separate read() calls
    // (proving the position cursor genuinely advances between calls,
    // not just replaying the whole buffer each time).
    int wfd = open("/system/posix.txt", 1);
    write(wfd, "POSIX ", 6);
    write(wfd, "shim works!", 11);
    close(wfd);

    int rfd = open("/system/posix.txt", 0);
    char posixBuf1[8];
    int n1 = read(rfd, &posixBuf1[0], 6);
    posixBuf1[n1] = 0;
    char posixBuf2[16];
    int n2 = read(rfd, &posixBuf2[0], 11);
    posixBuf2[n2] = 0;
    close(rfd);

    doSyscall(1, (u64) "POSIX read() 1: ", 0, 0);
    doSyscall(1, (u64) &posixBuf1[0], 0, 0);
    doSyscall(1, (u64) "POSIX read() 2: ", 0, 0);
    doSyscall(1, (u64) &posixBuf2[0], 0, 0);

    // Milestone 23: block on the ring3-dedicated channel (index 1 - a
    // fixed, documented convention matching kmain.mc's boot-time
    // createChannel() ordering, the same class of "fixed demo value"
    // File's hardcoded path already relies on) until the shell's
    // `ring3go` command sends a trigger. This reuses milestone 15's
    // already-proven blocking mechanism, just called from a real ring3
    // syscall (number 8) for the first time instead of a kernel task
    // calling channelReceive() directly.
    //
    // Milestone 25: index 1 no longer goes straight into a syscall -
    // open() turns it into a real handle first, and the kernel's own
    // policy (syscall.mc's num 9) grants that handle RECEIVE rights
    // only, never SEND. The unauthorized send() attempt right after is
    // the actual proof this milestone exists: without real per-handle
    // rights enforcement, nothing would stop a ring3 process from
    // sending on a channel it was only ever given to listen on.
    Channel spawnTrigger;
    bool opened = spawnTrigger.open(1);
    doSyscall(1, (u64) "Channel.open() ok=0x", (u64) opened, 0);

    bool unauthorizedSendOk = spawnTrigger.send(0xDEADBEEF);
    doSyscall(1, (u64) "unauthorized Channel.send() succeeded=0x", (u64) unauthorizedSendOk, 0);

    u64 triggerValue = spawnTrigger.receive();
    doSyscall(1, (u64) "Channel.receive() got trigger 0x", triggerValue, 0);

    // Milestone 26: trigger value 0x2 (shell's `ring3fault` command,
    // distinct from `ring3go`'s 0x1) deliberately attempts a forbidden
    // write instead of spawning - the negative-space proof that
    // cloneAddressSpace() (mm/paging.mc) really does strip the user bit
    // from a cloned address space's shared PDPT[0]/[1] entries. 0x100000
    // is the kernel's own multiboot load address: definitely present
    // (boot.s's static identity map), definitely inside PDPT[0], and
    // definitely NOT something this process ever mapped for itself - if
    // the fix works, this write takes a real page fault (CR2=0x100000)
    // and the kernel's existing isr.mc handler halts before the line
    // below ever runs; if it somehow succeeded, that line running at all
    // - visible in the serial log - is the bug report.
    if (triggerValue == 2) {
        doSyscall(1, (u64) "attempting forbidden ring3 write to 0x", 0x100000, 0);
        u64* forbidden = (u64*) 0x100000;
        *forbidden = 0xDEADBEEF;
        doSyscall(1, (u64) "forbidden write succeeded (BUG!) at 0x", 0x100000, 0);
    } else {
        // Only reachable after receive() above unblocks - see the file
        // header comment for why this is what prevents infinite self-spawn
        // rather than needing a recursion-guard flag. loadVaddr/stackVaddr
        // must match the exact same 0x80000000/0x80020000 pair every other
        // call site uses - see the top-of-file comment for why stackVaddr
        // moved off 0x80001000.
        Process childImage;
        childImage.path = "/system/testprog.bin";
        u64 childTaskIndex = childImage.spawn(0x80000000, 0x80020000);
        doSyscall(1, (u64) "Process.spawn() launched taskIndex 0x", childTaskIndex, 0);

        // Milestone 27's proof, only reachable once the spawn above
        // actually succeeded (a real taskIndex, not the -1 sentinel).
        if (childTaskIndex != (u64) -1) {
            ProcessHandle noRights;
            bool openedNoRights = noRights.open((int) childTaskIndex, 0);
            doSyscall(1, (u64) "ProcessHandle.open(rights=0) ok=0x", (u64) openedNoRights, 0);
            u64 unauthorizedQuery = noRights.query();
            doSyscall(1, (u64) "unauthorized ProcessHandle.query() got 0x", unauthorizedQuery, 0);

            ProcessHandle queryRights;
            bool openedQuery = queryRights.open((int) childTaskIndex, RIGHT_QUERY);
            doSyscall(1, (u64) "ProcessHandle.open(RIGHT_QUERY) ok=0x", (u64) openedQuery, 0);
            u64 authorizedQuery = queryRights.query();
            doSyscall(1, (u64) "authorized ProcessHandle.query() got taskIndex 0x", authorizedQuery, 0);
        }
    }

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

// Channel/Process: wrapping numbers 6/7/8/9 the same way File wrapped
// 4/5 - real methods, not raw syscall numbers, over the same
// doSyscall() staging helper.
//
// Milestone 25: Channel.open() is new - turns a raw channel index into
// a real, rights-checked handle (syscall 9), which is what send()/
// receive() below now pass instead of the old raw index. The kernel
// decides the actual rights granted (see syscall.mc's num 9), not this
// call - open() just reports whether the kernel granted *something*.
bool Channel.open(Channel* self, int channelIndex) {
    u64 result = doSyscall(9, (u64) channelIndex, 0, 0);
    if (result == (u64) -1) {
        return false;
    }
    self->handle = (int) result;
    return true;
}

// Deliberately exercised by this file's own demo now (milestone 25's
// whole point): the boot-time process's own handle only ever has
// RIGHT_RECEIVE, so this call is EXPECTED to fail (return false) - see
// _start()'s comment for why that's the actual proof, not a bug.
bool Channel.send(Channel* self, u64 value) {
    u64 result = doSyscall(7, (u64) self->handle, value, 0);
    return result != (u64) -1;
}

u64 Channel.receive(Channel* self) {
    return doSyscall(8, (u64) self->handle, 0, 0);
}

u64 Process.spawn(Process* self, u64 loadVaddr, u64 stackVaddr) {
    return doSyscall(6, (u64) self->path, loadVaddr, stackVaddr);
}

// Milestone 27: ProcessHandle.open() requests a rights bitmask (syscall
// 10) rather than being handed a fixed one the way Channel.open() is -
// the kernel still decides the real policy (intersecting the request
// against RIGHT_QUERY, the only grantable Process right today), but the
// REQUEST itself is what lets this file demonstrate both an
// intentionally rights-less handle and a genuinely authorized one from
// the same call site, rather than needing a kernel-side testing backdoor.
bool ProcessHandle.open(ProcessHandle* self, int taskIndex, int requestedRights) {
    u64 result = doSyscall(10, (u64) taskIndex, (u64) requestedRights, 0);
    if (result == (u64) -1) {
        return false;
    }
    self->handle = (int) result;
    return true;
}

u64 ProcessHandle.query(ProcessHandle* self) {
    return doSyscall(3, (u64) self->handle, 0, 0);
}

// Milestone 24's POSIX shim - see the file header comment for the
// design. mode 0 = read (load the file's existing content up front),
// mode 1 = write (start an empty buffer, flushed as a new file on
// close). Plain free functions, deliberately not methods - matching
// POSIX's real API shape, not extending the native OO-style one.
FileDescriptor gFdTable[4];

int open(char* path, int mode) {
    int fd = -1;
    int i = 0;
    while (i < 4) {
        if (!gFdTable[i].used) {
            fd = i;
            break;
        }
        i = i + 1;
    }
    if (fd < 0) {
        return -1;
    }
    gFdTable[fd].used = true;
    gFdTable[fd].forWriting = (mode == 1);
    gFdTable[fd].path = path;
    gFdTable[fd].position = 0;
    if (mode == 1) {
        gFdTable[fd].length = 0;
    } else {
        File f;
        f.path = path;
        u64 n = f.read((char*) &gFdTable[fd].buffer[0], 255);
        if (n == (u64) -1) {
            gFdTable[fd].used = false;
            return -1;
        }
        gFdTable[fd].length = n;
    }
    return fd;
}

int read(int fd, char* buf, int len) {
    if (fd < 0 || fd >= 4 || !gFdTable[fd].used) {
        return -1;
    }
    u64 remaining = gFdTable[fd].length - gFdTable[fd].position;
    u64 n = (u64) len;
    if (n > remaining) {
        n = remaining;
    }
    u64 i = 0;
    while (i < n) {
        buf[i] = (char) gFdTable[fd].buffer[gFdTable[fd].position + i];
        i = i + 1;
    }
    gFdTable[fd].position = gFdTable[fd].position + n;
    return (int) n;
}

int write(int fd, char* buf, int len) {
    if (fd < 0 || fd >= 4 || !gFdTable[fd].used) {
        return -1;
    }
    int i = 0;
    while (i < len && gFdTable[fd].length < 256) {
        gFdTable[fd].buffer[gFdTable[fd].length] = (u8) buf[i];
        gFdTable[fd].length = gFdTable[fd].length + 1;
        i = i + 1;
    }
    return i;
}

int close(int fd) {
    if (fd < 0 || fd >= 4 || !gFdTable[fd].used) {
        return -1;
    }
    int result = 0;
    if (gFdTable[fd].forWriting) {
        File f;
        f.path = gFdTable[fd].path;
        u64 written = f.write((char*) &gFdTable[fd].buffer[0], gFdTable[fd].length);
        if (written == (u64) -1) {
            result = -1;
        }
    }
    gFdTable[fd].used = false;
    return result;
}
