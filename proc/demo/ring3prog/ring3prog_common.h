#pragma once

// Shared typedefs + syscall-wrapper helpers for every proc/demo/ring3prog/
// ring3prog_*.c split file (originally one 1380-line ring3prog.c - split
// for the same reason gui_toolkit.h/syscall.c/shell.c were split earlier
// this session). Most of this is duplicated per translation unit via this
// header (each split .c file gets its own private copy of these small,
// stateless-or-self-contained helpers - harmless for a ring3 demo binary)
// - g_msg_buf/g_msg_extra_len are the one real exception: channel_receive_full()
// is called exactly once, in ring3prog.c's own _start() prologue, and
// trigger 27 (ring3prog_ipc.c) reads back what it wrote, so those two stay
// real externs defined once in ring3prog.c, not per-file statics.
#include "../../../types.h"
#include "../../gui_toolkit.h"
#include "../../posix/posix.h"

// CLAUDE.md's own documented reason: -fPIC without hidden visibility makes
// gcc materialize any reference to an externally-linkable symbol via
// sym@GOTPCREL(%rip) (an ELF64-only relocation as --32 can't parse) - the
// -fvisibility=hidden CLI flag only covers symbols DEFINED in the
// compiling translation unit, not a bare `extern` declaration of a symbol
// defined elsewhere (g_msg_buf/g_msg_extra_len below) - every header needs
// this same explicit wrap, first hit here because this is the first ring3
// program header with a real cross-file extern.
#pragma GCC visibility push(hidden)

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

typedef struct {
    int handle;
} process_handle;

typedef struct {
    int id;
} window;

typedef struct __attribute__((packed)) {
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    u32 body_color;
    u32 title_color;
} window_create_args;

typedef struct __attribute__((packed)) {
    int id;
    u32 x;
    u32 y;
    u32 width;
    u32 height;
    u32 color;
} window_fill_rect_args;

typedef struct __attribute__((packed)) {
    int id;
    u32 x;
    u32 y;
    u32 fg_color;
    u32 bg_color;
    char* text;
} window_draw_text_args;

typedef struct {
    i32 x;
    i32 y;
    u8 buttons;
} mouse_state;

typedef struct {
    bool used;
    bool for_writing;
    char* path;
    u64 position;
    u64 length;
    u8 buffer[256];
} file_descriptor;

static __attribute__((unused)) u64 do_syscall(u64 num, u64 arg1, u64 arg2, u64 arg3) {
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

static __attribute__((unused)) u64 file_write(file* self, char* buf, u64 len) {
    return do_syscall(5, (u64) self->path, (u64) buf, len);
}

static __attribute__((unused)) u64 file_read(file* self, char* buf, u64 max_len) {
    return do_syscall(4, (u64) self->path, (u64) buf, max_len);
}

static __attribute__((unused)) u8 g_read_buf[64];
static __attribute__((unused)) u8 g_async_buf[64];

// Real Thread object demo (trigger 23, Faza I point 3) - a real shared
// global both the main thread and a spawned thread touch, proving genuine
// concurrent execution (not the main thread quietly doing the work
// itself): the spawned thread increments this THREAD_COUNTER_TARGET
// times then exits; the main thread reads it before create, right after
// create (racy/low, expected), and after join (must be exactly the
// target) - three real, checkable snapshots.
#define THREAD_COUNTER_TARGET 1000
static __attribute__((unused)) volatile u64 g_thread_counter;

static __attribute__((unused)) void thread_counter_entry(void) {
    int i = 0;
    while (i < THREAD_COUNTER_TARGET) {
        g_thread_counter = g_thread_counter + 1;
        i = i + 1;
    }
    gt_thread_exit();
}

// Real Event/Mutex/Timer demo (trigger 24, Faza I point 2) - two threads
// contend for the SAME mutex-protected counter (the real proof mutex_lock/
// unlock prevent lost updates under real preemption) while also
// incrementing an unprotected twin with no locking at all, for contrast.
#define SYNC_COUNTER_TARGET_PER_THREAD 5000
static __attribute__((unused)) volatile u64 g_protected_counter;
static __attribute__((unused)) volatile u64 g_unprotected_counter;
static __attribute__((unused)) int g_sync_mutex_handle;

static __attribute__((unused)) void sync_counter_entry(void) {
    int i = 0;
    while (i < SYNC_COUNTER_TARGET_PER_THREAD) {
        gt_mutex_lock(g_sync_mutex_handle);
        g_protected_counter = g_protected_counter + 1;
        gt_mutex_unlock(g_sync_mutex_handle);

        g_unprotected_counter = g_unprotected_counter + 1;
        i = i + 1;
    }
    gt_thread_exit();
}

// Event ordering: this thread does real (bounded-loop) work, sets a real
// flag, THEN signals - the main thread's own gt_event_wait() must never
// observe the flag still false once it returns.
static __attribute__((unused)) volatile bool g_event_work_done;
static __attribute__((unused)) int g_sync_event_handle;

static __attribute__((unused)) void event_signal_entry(void) {
    int i = 0;
    while (i < 200) {
        i = i + 1;
    }
    g_event_work_done = true;
    gt_event_signal(g_sync_event_handle);
    gt_thread_exit();
}

// Real cross-process SharedMemory round-trip (trigger 25/26, Faza I
// point 8) - distinct from trigger 19's own vaddrs (0x80300000/
// 0x80400000), purely for clarity since these live in a separate
// process's own address space anyway.
#define SHM_SYNC_VADDR 0x80500000

static __attribute__((unused)) bool channel_open(channel* self, int channel_index) {
    u64 result = do_syscall(9, (u64) channel_index, 0, 0);
    if (result == (u64) -1) {
        return false;
    }
    self->handle = (int) result;
    return true;
}

static __attribute__((unused)) bool channel_send(channel* self, u64 value) {
    u64 result = do_syscall(7, (u64) self->handle, value, 0);
    return result != (u64) -1;
}

// Faza I point 8 item 4: real structured Channel payload, beyond one raw
// u64 (syscall 86 / gt_channel_receive_msg). A channel is a single-slot
// mailbox, so a trigger's own value and any extra structured data for it
// must travel together as ONE message rather than two separate sends
// (two sends would race/fail - the second one hits "full" before this
// process has drained the first, a real bug found while building
// trigger 27 below). First 8 bytes are always the trigger value (every
// existing trigger only ever sends exactly 8 bytes via channel_send(),
// so this replaces the old u64-only channel_receive()/syscall 8 at the
// top of _start()'s dispatch outright) - anything beyond that is the
// trigger-specific extra payload, left in g_msg_buf/g_msg_extra_len for
// whichever branch needs it.
#define RING3_MSG_BUF_MAX 128  // matches CHANNEL_MSG_MAX (proc/ipc/channel/channel.h)
extern u8 g_msg_buf[RING3_MSG_BUF_MAX];  // defined in ring3prog.c, populated once by channel_receive_full() in _start()'s own prologue
extern u32 g_msg_extra_len;

static __attribute__((unused)) u64 channel_receive_full(channel* self) {
    u32 total = gt_channel_receive_msg(self->handle, &g_msg_buf[0], RING3_MSG_BUF_MAX);
    u64 value = 0;
    if (total >= sizeof(value)) {
        value = *(u64*) &g_msg_buf[0];
        g_msg_extra_len = total - (u32) sizeof(value);
    } else {
        g_msg_extra_len = 0;
    }
    return value;
}

static __attribute__((unused)) u64 process_spawn(process* self, u64 load_vaddr, u64 stack_vaddr) {
    return do_syscall(6, (u64) self->path, load_vaddr, stack_vaddr);
}

static __attribute__((unused)) bool process_handle_open(process_handle* self, int task_index, int requested_rights) {
    u64 result = do_syscall(10, (u64) task_index, (u64) requested_rights, 0);
    if (result == (u64) -1) {
        return false;
    }
    self->handle = (int) result;
    return true;
}

static __attribute__((unused)) u64 process_handle_query(process_handle* self) {
    return do_syscall(3, (u64) self->handle, 0, 0);
}

static __attribute__((unused)) bool window_create(window* self, i32 x, i32 y, u32 width, u32 height,
                           u32 body_color, u32 title_color) {
    window_create_args args;
    args.x = x;
    args.y = y;
    args.width = width;
    args.height = height;
    args.body_color = body_color;
    args.title_color = title_color;
    u64 result = do_syscall(26, (u64) &args, 0, 0);
    if (result == (u64) -1) {
        return false;
    }
    self->id = (int) result;
    return true;
}

static __attribute__((unused)) bool window_move(window* self, i32 x, i32 y) {
    u64 result = do_syscall(27, (u64) self->id, (u64) x, (u64) y);
    return result != (u64) -1;
}

static __attribute__((unused)) bool window_raise(window* self) {
    u64 result = do_syscall(28, (u64) self->id, 0, 0);
    return result != (u64) -1;
}

static __attribute__((unused)) bool window_close(window* self) {
    u64 result = do_syscall(29, (u64) self->id, 0, 0);
    return result != (u64) -1;
}

static __attribute__((unused)) bool window_fill_rect(window* self, u32 x, u32 y, u32 width, u32 height, u32 color) {
    window_fill_rect_args args;
    args.id = self->id;
    args.x = x;
    args.y = y;
    args.width = width;
    args.height = height;
    args.color = color;
    u64 result = do_syscall(30, (u64) &args, 0, 0);
    return result != (u64) -1;
}

static __attribute__((unused)) bool window_draw_text(window* self, u32 x, u32 y, char* text, u32 fg_color, u32 bg_color) {
    window_draw_text_args args;
    args.id = self->id;
    args.x = x;
    args.y = y;
    args.fg_color = fg_color;
    args.bg_color = bg_color;
    args.text = text;
    u64 result = do_syscall(32, (u64) &args, 0, 0);
    return result != (u64) -1;
}

static __attribute__((unused)) void mouse_query(mouse_state* self) {
    u64 packed = do_syscall(31, 0, 0, 0);
    self->x = (i32) (packed & 0xFFFF);
    self->y = (i32) ((packed >> 16) & 0xFFFF);
    self->buttons = (u8) ((packed >> 32) & 0xFF);
}

// mode 0 = read, mode 1 = write (flushed as a new file on close).
static __attribute__((unused)) file_descriptor g_fd_table[4];

static __attribute__((unused)) int posix_open(char* path, int mode) {
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

static __attribute__((unused)) int posix_read(int fd, char* buf, int len) {
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

static __attribute__((unused)) int posix_write(int fd, char* buf, int len) {
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

static __attribute__((unused)) int posix_close(int fd) {
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

// Runs the same File/POSIX/Channel self-checks every boot always has -
// kept as real, working plumbing (the channel_open here is what every
// ring3* trigger command depends on), just silenced by default so booting
// doesn't dump ~15 lines nobody asked to see. The trigger-specific prints
// below (once a trigger actually arrives) are unaffected - those only
// fire when a shell command explicitly asks for them.

#pragma GCC visibility pop
