// First real ring3-side GUI toolkit piece (Faza II point 21) - a Button
// widget on top of the window/font/mouse syscalls. Self-contained (own
// syscall wrapper, own arg structs) so it can be #include-d by any future
// ring3 program, unlike every ring3 .c file so far which duplicates this
// stuff inline with zero sharing. Takes a plain window id (int), not a
// window* struct, so it has no dependency on whatever window type the
// including .c file defines.

#pragma once
#include "../types.h"

typedef struct __attribute__((packed)) {
    int id;
    u32 x;
    u32 y;
    u32 width;
    u32 height;
    u32 color;
} gt_window_fill_rect_args;

typedef struct __attribute__((packed)) {
    int id;
    u32 x;
    u32 y;
    u32 fg_color;
    u32 bg_color;
    char* text;
} gt_window_draw_text_args;

typedef struct __attribute__((packed)) {
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    u32 body_color;
} gt_window_create_borderless_args;

typedef struct __attribute__((packed)) {
    i32 x;
    i32 y;
    u32 width;
    u32 height;
    u32 body_color;
    u32 title_color;
} gt_window_create_args;

typedef struct __attribute__((packed)) {
    u64 since_pos;
    char* out_buf;
    u32 max_len;
} gt_term_read_args;

typedef struct __attribute__((packed)) {
    char* dir_path;
    int index;
    char* name_out;
    u32* size_out;
    bool* is_dir_out;
} gt_fs_list_args;

static u64 gt_syscall(u64 num, u64 arg1, u64 arg2, u64 arg3) {
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

// Ordinary bordered window - same as ring3prog.c's own window_create()
// wrapper, duplicated here so gui_toolkit.h stays self-contained. Returns
// -1 on failure.
static __attribute__((unused)) int gt_window_create(i32 x, i32 y, u32 width, u32 height, u32 body_color, u32 title_color) {
    gt_window_create_args args;
    args.x = x;
    args.y = y;
    args.width = width;
    args.height = height;
    args.body_color = body_color;
    args.title_color = title_color;
    u64 result = gt_syscall(26, (u64) &args, 0, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

// Returns false if id is invalid. body_x/body_y/body_width/body_height are
// screen coordinates/dims of the window's body (past the titlebar) -
// matches window_fill_content_rect/window_draw_text's own coordinate
// origin, so callers here never need to know TITLEBAR_HEIGHT.
static __attribute__((unused)) bool gt_window_query(int id, i32* body_x, i32* body_y, u32* body_width, u32* body_height) {
    u64 packed = gt_syscall(33, (u64) id, 0, 0);
    if (packed == (u64) -1) {
        return false;
    }
    *body_x = (i32) (packed & 0xFFFF);
    *body_y = (i32) ((packed >> 16) & 0xFFFF);
    *body_width = (u32) ((packed >> 32) & 0xFFFF);
    *body_height = (u32) ((packed >> 48) & 0xFFFF);
    return true;
}

static __attribute__((unused)) void gt_mouse_query(i32* x, i32* y, u8* buttons) {
    u64 packed = gt_syscall(31, 0, 0, 0);
    *x = (i32) (packed & 0xFFFF);
    *y = (i32) ((packed >> 16) & 0xFFFF);
    *buttons = (u8) ((packed >> 32) & 0xFF);
}

// Same slot/z-order rules as window_create, minus a titlebar - see
// kernel/gfx/window.h's window_create_borderless(). Returns -1 on failure.
static __attribute__((unused)) int gt_window_create_borderless(i32 x, i32 y, u32 width, u32 height, u32 body_color) {
    gt_window_create_borderless_args args;
    args.x = x;
    args.y = y;
    args.width = width;
    args.height = height;
    args.body_color = body_color;
    u64 result = gt_syscall(34, (u64) &args, 0, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

// Raw PIT ticks since boot (isr.c's g_tick_count) - uptime, not wall-clock
// time.
static __attribute__((unused)) u64 gt_get_ticks(void) {
    return gt_syscall(35, 0, 0, 0);
}

// Real wall-clock time (always 24-hour), read from the CMOS RTC via
// syscall 42 - kernel/drivers/rtc/rtc.c.
static __attribute__((unused)) void gt_get_time(u8* hour, u8* minute, u8* second) {
    u64 packed = gt_syscall(42, 0, 0, 0);
    *hour = (u8) ((packed >> 16) & 0xFF);
    *minute = (u8) ((packed >> 8) & 0xFF);
    *second = (u8) (packed & 0xFF);
}

// Real date (day/month/year, year = 2000 + RTC's 2-digit year - no
// century register read), read via syscall 43 - kernel/drivers/rtc/rtc.c.
static __attribute__((unused)) void gt_get_date(u8* day, u8* month, u16* year) {
    u64 packed = gt_syscall(43, 0, 0, 0);
    *day = (u8) ((packed >> 24) & 0xFF);
    *month = (u8) ((packed >> 16) & 0xFF);
    *year = (u16) (packed & 0xFFFF);
}

static __attribute__((unused)) bool gt_window_close(int id) {
    return gt_syscall(29, (u64) id, 0, 0) != 0;
}

// Real persistent open-file objects (proc/ipc/file/file.h via syscalls
// 44-48) - open once, read/write/seek incrementally, close - unlike
// gt_vfs_read/gt_vfs_write's one-shot open+op+close. mode 0=read/1=write;
// the rights the handle gets are fixed at open time (RIGHT_READ or
// RIGHT_WRITE, never both), so e.g. a read-mode handle's gt_file_write()
// call is rejected by the kernel, not just by convention.
static __attribute__((unused)) int gt_file_open(const char* path, int mode) {
    u64 result = gt_syscall(44, (u64) path, (u64) mode, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

static __attribute__((unused)) int gt_file_read(int handle, u8* buf, u32 max_len) {
    return (int) gt_syscall(45, (u64) handle, (u64) buf, (u64) max_len);
}

static __attribute__((unused)) int gt_file_write(int handle, const u8* data, u32 len) {
    return (int) gt_syscall(46, (u64) handle, (u64) data, (u64) len);
}

static __attribute__((unused)) bool gt_file_seek(int handle, u32 pos) {
    return gt_syscall(47, (u64) handle, (u64) pos, 0) != (u64) -1;
}

static __attribute__((unused)) bool gt_file_close(int handle) {
    return gt_syscall(48, (u64) handle, 0, 0) != (u64) -1;
}

// Real UID-based file ownership/permission bits - values must match
// kernel/fs/minifs/minifs.h's MODE_OWNER_ONLY_READ/WRITE exactly (same
// kernel-constant-mirrored-in-ring3 duplication this file's own
// RIGHT_QUERY-style constants already use elsewhere - ring3 code can't
// include a kernel-internal header). Enforcement itself happens in
// proc/ipc/file/file.c's file_object_open().
#define MODE_OWNER_ONLY_READ 1
#define MODE_OWNER_ONLY_WRITE 2
static __attribute__((unused)) bool gt_setuid(u8 uid) {
    return gt_syscall(49, (u64) uid, 0, 0) != (u64) -1;
}

// path is bare MiniFS-relative (e.g. "permtest.mfs"), not VFS-absolute -
// same convention gt_fs_mkdir/gt_fs_delete already use.
static __attribute__((unused)) bool gt_fs_set_owner(const char* path, u8 uid) {
    return gt_syscall(50, (u64) path, (u64) uid, 0) != (u64) -1;
}

static __attribute__((unused)) bool gt_fs_set_mode(const char* path, u8 mode) {
    return gt_syscall(51, (u64) path, (u64) mode, 0) != (u64) -1;
}

// Real byte-stream Pipe (proc/ipc/pipe/pipe.h) - unlike a Channel's
// single-value mailbox, a real FIFO with partial-read/partial-write
// semantics. mode 0=receive/1=send, same shape as gt_file_open's mode.
static __attribute__((unused)) int gt_pipe_open(int raw_index, int mode) {
    u64 result = gt_syscall(52, (u64) raw_index, (u64) mode, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

static __attribute__((unused)) int gt_pipe_write(int handle, const u8* data, u32 len) {
    return (int) gt_syscall(53, (u64) handle, (u64) data, (u64) len);
}

static __attribute__((unused)) int gt_pipe_read(int handle, u8* buf, u32 max_len) {
    return (int) gt_syscall(54, (u64) handle, (u64) buf, (u64) max_len);
}

// Real frame-backed SharedMemory (proc/ipc/shared_memory/shared_memory.h)
// - RIGHT_MAP, the roadmap's own literal "Handle<File> READ/WRITE/MAP".
static __attribute__((unused)) int gt_shm_create(u32 size) {
    u64 result = gt_syscall(55, (u64) size, 0, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

// Maps into the CALLER's own address space at vaddr.
static __attribute__((unused)) bool gt_shm_map(int handle, u64 vaddr) {
    return gt_syscall(56, (u64) handle, vaddr, 0) != (u64) -1;
}

// Maps into a DIFFERENT process's address space (e.g. a child just
// spawned via gt_syscall(6, ...)/process_spawn, which hands back its
// real task_index) - deliberately permissive, no ownership check on the
// target.
static __attribute__((unused)) bool gt_shm_map_into(int handle, u64 target_task_index, u64 vaddr) {
    return gt_syscall(57, (u64) handle, target_task_index, vaddr) != (u64) -1;
}

// Spawns one of the fixed compiled-in GUI apps (0=terminal, 1=file_manager,
// 2=settings - kernel/syscall/syscall.c's own gui_app_bounds()) instead of
// kmain.c auto-spawning all of them at boot. Returns the new process index,
// or -1 on failure (unknown app id, or the process/task table is full).
static __attribute__((unused)) int gt_spawn_app(int app_id) {
    u64 result = gt_syscall(41, (u64) app_id, 0, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

// Copies console-shell output produced since since_pos into out_buf (up
// to max_len bytes) - the same text every cmd_* already prints via
// vga_print/serial_print, mirrored kernel-side (kernel/drivers/io.c). Returns
// the number of bytes actually copied (0 if already caught up). If the
// caller fell more than TERM_SCROLLBACK_SIZE bytes behind, some output
// is silently lost - not resolved further than that (see syscall 36's
// comment in kernel/syscall/syscall.c).
static __attribute__((unused)) u32 gt_term_read(u64 since_pos, char* out_buf, u32 max_len) {
    gt_term_read_args args;
    args.since_pos = since_pos;
    args.out_buf = out_buf;
    args.max_len = max_len;
    return (u32) gt_syscall(36, (u64) &args, 0, 0);
}

// Lists one entry (0..MINIFS_MAX_FILES-1) of dir_path ("" = /system
// root). Returns false past the last used slot or an unresolvable
// dir_path - same as fs_list_entry (kernel/fs/minifs.c), which this wraps
// via syscall 37.
static __attribute__((unused)) bool gt_fs_list(char* dir_path, int index, char* name_out, u32* size_out, bool* is_dir_out) {
    gt_fs_list_args args;
    args.dir_path = dir_path;
    args.index = index;
    args.name_out = name_out;
    args.size_out = size_out;
    args.is_dir_out = is_dir_out;
    return gt_syscall(37, (u64) &args, 0, 0) != 0;
}

// Flat removal (no child-cascade check on a directory) - wraps
// fs_delete_file via syscall 38.
static __attribute__((unused)) bool gt_fs_delete(char* path) {
    return gt_syscall(38, (u64) path, 0, 0) != 0;
}

// Wraps fs_create_dir via syscall 39.
static __attribute__((unused)) bool gt_fs_mkdir(char* path) {
    return gt_syscall(39, (u64) path, 0, 0) != 0;
}

// Wraps syscall 5 (vfs_write) - create-only-fails-if-exists, same as
// every other vfs_write caller (cmd_mkfile, install, ...).
static __attribute__((unused)) bool gt_vfs_write(char* path, u8* data, u32 len) {
    return gt_syscall(5, (u64) path, (u64) data, (u64) len) != (u64) -1;
}

// Wraps syscall 4 (vfs_read) - same raw (path, buf, max_len) shape as
// ring3prog.c's own direct do_syscall(4, ...) call. Returns bytes read,
// -1 (not found) or -2 (too big for max_len).
static __attribute__((unused)) int gt_vfs_read(char* path, u8* buf, u32 max_len) {
    return (int) gt_syscall(4, (u64) path, (u64) buf, (u64) max_len);
}

typedef struct __attribute__((packed)) {
    u32* total_frames_out;
    u32* free_frames_out;
    u32* disk_file_count_out;
} gt_sys_info_args;

// Wraps syscall 40 - live kernel stats (frame allocator + MiniFS
// superblock), read straight from kernel globals, no caching.
static __attribute__((unused)) bool gt_sys_info(u32* total_frames_out, u32* free_frames_out, u32* disk_file_count_out) {
    gt_sys_info_args args;
    args.total_frames_out = total_frames_out;
    args.free_frames_out = free_frames_out;
    args.disk_file_count_out = disk_file_count_out;
    return gt_syscall(40, (u64) &args, 0, 0) == 0;
}

// Minimal, self-contained hex formatter - kernel/lib/strings.c's format_hex()
// isn't linked into ring3 programs (each is its own standalone-linked
// blob, see proc/ring3.ld). Null-terminates, unlike format_hex(), since
// window_draw_text's syscall dereferences a null-terminated string on
// the kernel side. Returns the digit count, not counting the terminator.
// Uppercase A-F, not lowercase - the font (kernel/gfx/font.h) has no lowercase
// glyphs at all, so a lowercase hex digit used to render as an invisible
// gap (found via Settings' memory stats, the first caller whose values
// routinely land above 0xF - desktop_shell's tick counts and File
// Manager's sizes happened to stay in the 0-9 range in every case tested
// so far, masking this until now).
static __attribute__((unused)) int gt_format_hex(u64 value, char* out) {
    const char* digits = "0123456789ABCDEF";
    if (value == 0) {
        out[0] = '0';
        out[1] = '\0';
        return 1;
    }
    char buf[16];
    int i = 15;
    while (value > 0 && i >= 0) {
        buf[i] = digits[value % 16];
        value = value / 16;
        i = i - 1;
    }
    int len = 15 - i;
    int j = 0;
    while (j < len) {
        out[j] = buf[i + 1 + j];
        j = j + 1;
    }
    out[len] = '\0';
    return len;
}

// TEMPORARY diagnostic helper for the button_poll self-corruption
// investigation ([[project_button_poll_crash_bug]] in project memory) -
// wraps syscall 1 (message + one hex value, tagged with task/process
// index kernel-side). Remove once that bug is root-caused.
static __attribute__((unused)) void gt_debug_print(const char* msg, u64 value) {
    gt_syscall(1, (u64) msg, value, 0);
}

typedef struct {
    int window_id;
    u32 x, y, width, height;  // body-local rect, passed to window_id at init
    char* label;
    u32 normal_color, pressed_color, label_color;
    bool was_down;  // last-poll left-button state, for click edge detection
    bool last_rendered_pressed;  // what button_draw() last actually drew -
                                  // button_poll() only redraws on a change
} button;

static __attribute__((unused)) void button_draw(button* self, bool pressed) {
    u32 color = pressed ? self->pressed_color : self->normal_color;

    gt_window_fill_rect_args fill_args;
    fill_args.id = self->window_id;
    fill_args.x = self->x;
    fill_args.y = self->y;
    fill_args.width = self->width;
    fill_args.height = self->height;
    fill_args.color = color;
    gt_syscall(30, (u64) &fill_args, 0, 0);

    // Small left/vertical padding, not true centering - real text-width
    // measurement (label centering) is a natural next toolkit addition,
    // out of scope for a first Button.
    gt_window_draw_text_args text_args;
    text_args.id = self->window_id;
    text_args.x = self->x + 4;
    text_args.y = self->y + (self->height > 9 ? (self->height - 7) / 2 : 1);
    text_args.fg_color = self->label_color;
    text_args.bg_color = color;
    text_args.text = self->label;
    gt_syscall(32, (u64) &text_args, 0, 0);
}

static __attribute__((unused)) void button_init(button* self, int window_id, u32 x, u32 y, u32 width, u32 height,
                         char* label, u32 normal_color, u32 pressed_color, u32 label_color) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;
    self->label = label;
    self->normal_color = normal_color;
    self->pressed_color = pressed_color;
    self->label_color = label_color;
    self->was_down = false;
    button_draw(self, false);
    self->last_rendered_pressed = false;
}

// Polls real mouse + window state (both live, not cached) and redraws the
// button pressed/normal - but ONLY when that visual state actually
// changes since the last poll. button_draw() erases-then-redraws the
// label as two separate syscalls/compositor_redraw() passes; calling it
// on every poll of an unthrottled forever-loop (desktop_shell.c has no
// yield/sleep syscall to pace itself with) made the label visibly blink
// continuously on a real display, even once compositor_redraw() itself
// stopped producing torn frames - a real bug, not the same one. Returns
// true exactly once per press-and-hold - on the down-transition while the
// cursor is inside the button - not on every poll while held, and not on
// release. No multi-window click arbitration yet (whichever window is on
// top doesn't matter here) - a real Window Server would gate this on
// focus/z-order too; that's still point 18's open "focus" item, not this
// widget's job.
static __attribute__((unused)) bool button_poll(button* self) {
    // TEMPORARY diagnostic guard - see gt_debug_print's own comment above
    // and [[project_button_poll_crash_bug]]. A real button is always a
    // static/local variable's address within this program's own private
    // region (BUILTIN_LOAD_VADDR=0x80000000 + a few KB at most, every
    // ring3 program here is tiny) - anything outside a generous margin of
    // that range means `self` itself got corrupted somewhere between
    // being computed at the call site and landing here, not a bug in
    // *this* function. Logs and bails out instead of dereferencing
    // garbage, so the kernel survives long enough to read the log.
    if ((u64) self < 0x80000000 || (u64) self > 0x80100000) {
        gt_debug_print("button_poll: BAD self ptr 0x", (u64) self);
        return false;
    }
    i32 win_x, win_y;
    u32 win_width, win_height;
    if (!gt_window_query(self->window_id, &win_x, &win_y, &win_width, &win_height)) {
        return false;
    }
    i32 mouse_x, mouse_y;
    u8 buttons;
    gt_mouse_query(&mouse_x, &mouse_y, &buttons);

    u32 screen_x = (u32) win_x + self->x;
    u32 screen_y = (u32) win_y + self->y;
    bool inside = (u32) mouse_x >= screen_x && (u32) mouse_x < screen_x + self->width
        && (u32) mouse_y >= screen_y && (u32) mouse_y < screen_y + self->height;
    bool left_down = (buttons & 1) != 0;
    bool clicked = inside && left_down && !self->was_down;
    self->was_down = left_down;

    bool pressed = inside && left_down;
    if (pressed != self->last_rendered_pressed) {
        button_draw(self, pressed);
        self->last_rendered_pressed = pressed;
    }
    return clicked;
}
