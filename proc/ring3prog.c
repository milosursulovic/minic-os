// A ring3 "program" - compiled standalone (see proc/ring3.ld and the
// Makefile's dedicated build rule for the standalone link+objcopy step
// that flattens this into one truly contiguous blob), loaded and
// entered via spawn_process()'s "copy one byte range, jump to its first
// byte" model.
//
// Exercises the native "File"/"Channel"/"Process"/"ProcessHandle" API
// (real methods in the MiniC-era version; here, plain functions taking
// an explicit `self` pointer - C has no method-call sugar, so
// `msg_file.write(...)` becomes `file_write(&msg_file, ...)`) and a
// thin POSIX-shaped shim (open/read/write/close, deliberately plain
// free functions, not wrapped - matching POSIX's real API shape rather
// than extending the native OO-style one), implemented entirely in this
// file with no kernel changes.
//
// Real GCC inline asm has real operand binding, so do_syscall() (and
// the NX test's function-pointer call near the bottom of _start) need
// none of the global-staging trick the MiniC-era version needed for
// every asm() block here - a genuine simplification, not just a port.
//
// spawn_process() always jumps to byte 0 of the loaded image (see
// process.c) - it has to be _start there, not wherever gcc happened to
// place it in source order. Relying on declaration order to keep _start
// first was tried and genuinely broke once this file grew past a single
// function (a real bug, found via `nm ring3prog_linked.elf` showing
// _start at offset 0x7d1 with do_syscall - the first function actually
// *declared* in this file - sitting at offset 0 instead; the CPU then
// ran straight into do_syscall's body as if it were the entry point).
// A dedicated linker-script section forces this regardless of source
// order or any future gcc reordering - see ring3.ld's ".text.start",
// and the `__attribute__((section(...)))` directly on _start() below.

#include "../types.h"

#define RIGHT_QUERY 1

typedef struct {
    char* path;
} file;

typedef struct {
    int handle;
} channel;

typedef struct {
    char* path;
} process;

// A handle-based counterpart to `process` (which is path-based, spawn-
// only) - wraps syscalls 10/3, the same shape `channel` wraps 9/7/8.
typedef struct {
    int handle;
} process_handle;

typedef struct {
    bool used;
    bool for_writing;
    char* path;
    u64 position;
    u64 length;
    u8 buffer[256];
} file_descriptor;

static u64 do_syscall(u64 num, u64 arg1, u64 arg2, u64 arg3) {
    u64 result;
    register u64 r_num __asm__("rax") = num;
    register u64 r_arg1 __asm__("rdi") = arg1;
    register u64 r_arg2 __asm__("rsi") = arg2;
    register u64 r_arg3 __asm__("rdx") = arg3;
    __asm__ volatile("int $0x80"
                      : "+r"(r_num)
                      : "r"(r_arg1), "r"(r_arg2), "r"(r_arg3)
                      : "memory");
    result = r_num;
    return result;
}

// The native "File" API: wraps the raw vfs_read/vfs_write syscalls
// (numbers 4/5). Deliberately minimal: the kernel's own vfs_read/
// vfs_write are already whole-file, path-based, stateless operations
// (no open/close, no seek/position), so a file is just a path plus real
// operations, not a new kernel-side file-descriptor mechanism no other
// part of this kernel needs yet.
static u64 file_write(file* self, char* buf, u64 len) {
    return do_syscall(5, (u64) self->path, (u64) buf, len);
}

static u64 file_read(file* self, char* buf, u64 max_len) {
    return do_syscall(4, (u64) self->path, (u64) buf, max_len);
}

static u8 g_read_buf[64];

// channel/process: wrapping numbers 6/7/8/9 the same way file wrapped
// 4/5.
//
// channel_open() turns a raw channel index into a real, rights-checked
// handle (syscall 9), which is what send()/receive() below now pass
// instead of a raw index. The kernel decides the actual rights granted
// (see syscall.c's num 9), not this call - open() just reports whether
// the kernel granted *something*.
static bool channel_open(channel* self, int channel_index) {
    u64 result = do_syscall(9, (u64) channel_index, 0, 0);
    if (result == (u64) -1) {
        return false;
    }
    self->handle = (int) result;
    return true;
}

// Deliberately exercised by this program's own demo: the boot-time
// process's own handle only ever has RIGHT_RECEIVE, so this call is
// EXPECTED to fail (return false) - see _start()'s comment for why
// that's the actual proof, not a bug.
static bool channel_send(channel* self, u64 value) {
    u64 result = do_syscall(7, (u64) self->handle, value, 0);
    return result != (u64) -1;
}

static u64 channel_receive(channel* self) {
    return do_syscall(8, (u64) self->handle, 0, 0);
}

static u64 process_spawn(process* self, u64 load_vaddr, u64 stack_vaddr) {
    return do_syscall(6, (u64) self->path, load_vaddr, stack_vaddr);
}

// process_handle_open() requests a rights bitmask (syscall 10) rather
// than being handed a fixed one the way channel_open() is - the kernel
// still decides the real policy (intersecting the request against
// RIGHT_QUERY, the only grantable process right today), but the REQUEST
// itself is what lets this program demonstrate both an intentionally
// rights-less handle and a genuinely authorized one from the same call
// site, rather than needing a kernel-side testing backdoor.
static bool process_handle_open(process_handle* self, int task_index, int requested_rights) {
    u64 result = do_syscall(10, (u64) task_index, (u64) requested_rights, 0);
    if (result == (u64) -1) {
        return false;
    }
    self->handle = (int) result;
    return true;
}

static u64 process_handle_query(process_handle* self) {
    return do_syscall(3, (u64) self->handle, 0, 0);
}

// The POSIX shim. mode 0 = read (load the file's existing content up
// front), mode 1 = write (start an empty buffer, flushed as a new file
// on close). Plain free functions, deliberately not "methods" - matching
// POSIX's real API shape, not extending the native OO-style one.
static file_descriptor g_fd_table[4];

static int posix_open(char* path, int mode) {
    int fd = -1;
    int i = 0;
    while (i < 4) {
        if (!g_fd_table[i].used) {
            fd = i;
            break;
        }
        i = i + 1;
    }
    if (fd < 0) {
        return -1;
    }
    g_fd_table[fd].used = true;
    g_fd_table[fd].for_writing = (mode == 1);
    g_fd_table[fd].path = path;
    g_fd_table[fd].position = 0;
    if (mode == 1) {
        g_fd_table[fd].length = 0;
    } else {
        file f;
        f.path = path;
        u64 n = file_read(&f, (char*) &g_fd_table[fd].buffer[0], 255);
        if (n == (u64) -1) {
            g_fd_table[fd].used = false;
            return -1;
        }
        g_fd_table[fd].length = n;
    }
    return fd;
}

static int posix_read(int fd, char* buf, int len) {
    if (fd < 0 || fd >= 4 || !g_fd_table[fd].used) {
        return -1;
    }
    u64 remaining = g_fd_table[fd].length - g_fd_table[fd].position;
    u64 n = (u64) len;
    if (n > remaining) {
        n = remaining;
    }
    u64 i = 0;
    while (i < n) {
        buf[i] = (char) g_fd_table[fd].buffer[g_fd_table[fd].position + i];
        i = i + 1;
    }
    g_fd_table[fd].position = g_fd_table[fd].position + n;
    return (int) n;
}

static int posix_write(int fd, char* buf, int len) {
    if (fd < 0 || fd >= 4 || !g_fd_table[fd].used) {
        return -1;
    }
    int i = 0;
    while (i < len && g_fd_table[fd].length < 256) {
        g_fd_table[fd].buffer[g_fd_table[fd].length] = (u8) buf[i];
        g_fd_table[fd].length = g_fd_table[fd].length + 1;
        i = i + 1;
    }
    return i;
}

static int posix_close(int fd) {
    if (fd < 0 || fd >= 4 || !g_fd_table[fd].used) {
        return -1;
    }
    int result = 0;
    if (g_fd_table[fd].for_writing) {
        file f;
        f.path = g_fd_table[fd].path;
        u64 written = file_write(&f, (char*) &g_fd_table[fd].buffer[0], g_fd_table[fd].length);
        if (written == (u64) -1) {
            result = -1;
        }
    }
    g_fd_table[fd].used = false;
    return result;
}

__attribute__((section(".text.start")))
void _start(void) {
    do_syscall(1, (u64) "hello from a LOADED process! 0x", 0xC0FFEE, 0);
    do_syscall(1, (u64) "hello from a LOADED process! 0x", 0xC0FFEE, 0);

    // handle 0 = myself (guaranteed by spawn_process()) - should resolve
    // to this process's own task_index, checkable against `ps`'s output.
    u64 self_task_index = do_syscall(3, 0, 0, 0);
    do_syscall(1, (u64) "handle 0 (self) -> task_index 0x", self_task_index, 0);

    // handle 99 was never allocated - should come back as the -1
    // sentinel (0xffffffffffffffff), not garbage and not a crash.
    u64 bad_result = do_syscall(3, 99, 0, 0);
    do_syscall(1, (u64) "handle 99 (invalid) -> 0x", bad_result, 0);

    // A real File object, real operations (msg_file/file_write/
    // file_read), from inside an actual ring3 process - not the kernel
    // calling vfs_write/vfs_read directly the way the shell's
    // `vfswrite`/`vfscat` commands do.
    file msg_file;
    msg_file.path = "/system/ring3msg.txt";
    u64 written = file_write(&msg_file, "hello from ring3, via a real File.write() method call!", 54);
    do_syscall(1, (u64) "File.write() wrote 0x", written, 0);

    u64 read_back = file_read(&msg_file, (char*) &g_read_buf[0], 63);
    g_read_buf[read_back] = 0;
    do_syscall(1, (u64) "File.read() got back 0x", read_back, 0);
    do_syscall(1, (u64) &g_read_buf[0], 0, 0);

    // The POSIX shim, exercised with two separate write() calls
    // (proving the buffer genuinely accumulates across calls, not just
    // capturing one big write) and two separate read() calls (proving
    // the position cursor genuinely advances between calls, not just
    // replaying the whole buffer each time).
    int wfd = posix_open("/system/posix.txt", 1);
    posix_write(wfd, "POSIX ", 6);
    posix_write(wfd, "shim works!", 11);
    posix_close(wfd);

    int rfd = posix_open("/system/posix.txt", 0);
    char posix_buf1[8];
    int n1 = posix_read(rfd, &posix_buf1[0], 6);
    posix_buf1[n1] = 0;
    char posix_buf2[16];
    int n2 = posix_read(rfd, &posix_buf2[0], 11);
    posix_buf2[n2] = 0;
    posix_close(rfd);

    do_syscall(1, (u64) "POSIX read() 1: ", 0, 0);
    do_syscall(1, (u64) &posix_buf1[0], 0, 0);
    do_syscall(1, (u64) "POSIX read() 2: ", 0, 0);
    do_syscall(1, (u64) &posix_buf2[0], 0, 0);

    // Block on the ring3-dedicated channel (index 1 - a fixed,
    // documented convention matching kmain.c's boot-time
    // create_channel() ordering) until the shell's `ring3go` command
    // sends a trigger. The boot-time process's own handle only ever has
    // RIGHT_RECEIVE, never RIGHT_SEND - so the unauthorized send()
    // attempt right after open() is EXPECTED to fail, the real proof
    // that per-handle rights enforcement is real, not just untested.
    channel spawn_trigger;
    bool opened = channel_open(&spawn_trigger, 1);
    do_syscall(1, (u64) "Channel.open() ok=0x", (u64) opened, 0);

    bool unauthorized_send_ok = channel_send(&spawn_trigger, 0xDEADBEEF);
    do_syscall(1, (u64) "unauthorized Channel.send() succeeded=0x", (u64) unauthorized_send_ok, 0);

    u64 trigger_value = channel_receive(&spawn_trigger);
    do_syscall(1, (u64) "Channel.receive() got trigger 0x", trigger_value, 0);

    // Trigger value 0x2 (shell's `ring3fault` command, distinct from
    // `ring3go`'s 0x1) deliberately attempts a forbidden write instead
    // of spawning - the negative-space proof that clone_address_space()
    // (mm/paging.c) really does strip the user bit from a cloned address
    // space's shared PDPT[0]/[1] entries. 0x100000 is the kernel's own
    // multiboot load address: definitely present (boot.s's static
    // identity map), definitely inside PDPT[0], and definitely NOT
    // something this process ever mapped for itself - if the fix works,
    // this write takes a real page fault (CR2=0x100000) and the
    // kernel's existing isr.c handler halts before the line below ever
    // runs; if it somehow succeeded, that line running at all is the
    // bug report.
    if (trigger_value == 2) {
        do_syscall(1, (u64) "attempting forbidden ring3 write to 0x", 0x100000, 0);
        u64* forbidden = (u64*) 0x100000;
        *forbidden = 0xDEADBEEF;
        do_syscall(1, (u64) "forbidden write succeeded (BUG!) at 0x", 0x100000, 0);
    } else if (trigger_value == 3) {
        // Trigger value 0x3 (shell's `ring3nx` command) is the negative-
        // space proof for NX enforcement on this process's own user
        // stack. 0x80020000 is the well-known stack_vaddr every spawn
        // call site uses - the very BOTTOM of the stack page, safely
        // below wherever this shallow call chain's real RSP currently
        // sits, so writing one byte there doesn't corrupt the live call
        // stack. 0xC3 is a real x86 `ret` opcode - harmless if it
        // somehow DID execute (it would just pop the return address the
        // call below is about to push and jump straight back).
        do_syscall(1, (u64) "attempting to execute ring3 stack byte at 0x", 0x80020000, 0);
        u8* stack_code = (u8*) 0x80020000;
        *stack_code = 0xC3;
        void (*fn)(void) = (void (*)(void)) stack_code;
        fn();
        do_syscall(1, (u64) "stack execution succeeded (BUG!) at 0x", 0x80020000, 0);
    } else {
        // Only reachable after receive() above unblocks - see the file
        // header comment for why this is what prevents infinite self-
        // spawn rather than needing a recursion-guard flag. load_vaddr/
        // stack_vaddr must match the exact same 0x80000000/0x80020000
        // pair every other call site uses.
        process child_image;
        child_image.path = "/system/testprog.bin";
        u64 child_task_index = process_spawn(&child_image, 0x80000000, 0x80020000);
        do_syscall(1, (u64) "Process.spawn() launched task_index 0x", child_task_index, 0);

        // Only reachable once the spawn above actually succeeded (a
        // real task_index, not the -1 sentinel).
        if (child_task_index != (u64) -1) {
            process_handle no_rights;
            bool opened_no_rights = process_handle_open(&no_rights, (int) child_task_index, 0);
            do_syscall(1, (u64) "ProcessHandle.open(rights=0) ok=0x", (u64) opened_no_rights, 0);
            u64 unauthorized_query = process_handle_query(&no_rights);
            do_syscall(1, (u64) "unauthorized ProcessHandle.query() got 0x", unauthorized_query, 0);

            process_handle query_rights;
            bool opened_query = process_handle_open(&query_rights, (int) child_task_index, RIGHT_QUERY);
            do_syscall(1, (u64) "ProcessHandle.open(RIGHT_QUERY) ok=0x", (u64) opened_query, 0);
            u64 authorized_query = process_handle_query(&query_rights);
            do_syscall(1, (u64) "authorized ProcessHandle.query() got task_index 0x", authorized_query, 0);
        }
    }

    for (;;) {
    }
}
