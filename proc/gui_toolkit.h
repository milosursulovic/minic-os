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

// Tells the kernel "this window id is the terminal" so the console
// shell's `exit` command can close it later.
static __attribute__((unused)) void gt_register_terminal_window(int window_id) {
    gt_syscall(58, (u64) window_id, 0, 0);
}

// Real generic Socket object (proc/ipc/socket/socket.h) - wraps
// kernel/net/tcp/tcp.c's real listen/accept, not just a client fetch.
static __attribute__((unused)) int gt_socket_listen(u16 port) {
    u64 result = gt_syscall(59, (u64) port, 0, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

// Fixed internal accept timeout (kernel/syscall/syscall.c, matches
// gt_pipe/gt_file's own shapes - 3 syscall args only).
static __attribute__((unused)) int gt_socket_accept(int handle) {
    u64 result = gt_syscall(60, (u64) handle, 0, 0);
    if (result == (u64) -1) {
        return -1;
    }
    return (int) result;
}

static __attribute__((unused)) int gt_socket_send(int handle, const u8* data, u16 len) {
    return (int) gt_syscall(61, (u64) handle, (u64) data, (u64) len);
}

static __attribute__((unused)) int gt_socket_receive(int handle, u8* buf, u32 max_len) {
    return (int) gt_syscall(62, (u64) handle, (u64) buf, (u64) max_len);
}

static __attribute__((unused)) bool gt_socket_close(int handle) {
    return gt_syscall(63, (u64) handle, 0, 0) != (u64) -1;
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

typedef struct __attribute__((packed)) {
    int index;
    char* name_out;
    int* category_out;
    u32* info_out;
} gt_device_list_args;

// Lists one entry (0..MAX_DEVICES-1) of the Device Manager registry -
// wraps kernel/drivers/device_manager/device_manager.h's device_manager_get
// via syscall 64. Returns false for an out-of-range or unused slot.
static __attribute__((unused)) bool gt_device_list(int index, char* name_out, int* category_out, u32* info_out) {
    gt_device_list_args args;
    args.index = index;
    args.name_out = name_out;
    args.category_out = category_out;
    args.info_out = info_out;
    return gt_syscall(64, (u64) &args, 0, 0) != 0;
}

typedef struct __attribute__((packed)) {
    int index;
    char* name_out;
    u32* flags_out;
    u32* restart_count_out;
} gt_service_list_args;

// Lists one entry (0..SERVICE_SLOTS-1) of the Service Manager registry -
// wraps kernel/services/service_manager.h's service_list_entry via
// syscall 65. flags_out packs used(bit0)/running(bit1)/auto_restart(bit2).
static __attribute__((unused)) bool gt_service_list(int index, char* name_out, u32* flags_out, u32* restart_count_out) {
    gt_service_list_args args;
    args.index = index;
    args.name_out = name_out;
    args.flags_out = flags_out;
    args.restart_count_out = restart_count_out;
    return gt_syscall(65, (u64) &args, 0, 0) != 0;
}

// Wraps service_start/stop/restart via syscalls 66/67/68 - "stop" means
// "don't respawn the next exit", not a forced kill (see
// kernel/services/service_manager.h's own comment on why).
static __attribute__((unused)) bool gt_service_start(char* name) {
    return gt_syscall(66, (u64) name, 0, 0) != 0;
}
static __attribute__((unused)) bool gt_service_stop(char* name) {
    return gt_syscall(67, (u64) name, 0, 0) != 0;
}
static __attribute__((unused)) bool gt_service_restart(char* name) {
    return gt_syscall(68, (u64) name, 0, 0) != 0;
}

// Real window focus (Faza II point 18) - wraps syscall 69. A window can
// request its own focus directly (a real "grab focus on open" pattern),
// not just receive it via a real mouse click.
static __attribute__((unused)) bool gt_focus_window(int window_id) {
    return gt_syscall(69, (u64) window_id, 0, 0) != 0;
}

// Returns the next queued keystroke for window_id if it's the currently
// focused window, else -1 - wraps syscall 70. Real ASCII: printable
// chars via the same g_scancode_table every console keystroke uses,
// '\n' for Enter, 0x08 for Backspace.
static __attribute__((unused)) int gt_read_key(int window_id) {
    return (int) gt_syscall(70, (u64) window_id, 0, 0);
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
    // region - real ASLR (kernel/lib/rand.h) now randomizes load_vaddr
    // within [0x80000000, 0x80000000 + ASLR_SLOTS*4096), so the upper
    // bound here is derived, not a guess: max load_vaddr
    // (0x80000000 + 511*4096 = 0x801FF000) + the 0x20000 stack gap every
    // spawn call site uses (0x8021F000) + one 4096-byte stack page
    // (0x80220000) - anything outside that range means `self` itself got
    // corrupted somewhere between being computed at the call site and
    // landing here, not a bug in *this* function. Logs and bails out
    // instead of dereferencing garbage, so the kernel survives long
    // enough to read the log.
    if ((u64) self < 0x80000000 || (u64) self > 0x80220000) {
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

// Label - real widget consolidating a pattern that was already duplicated
// (proc/apps/settings/settings.c's draw_static_label(), proc/apps/
// device_manager/device_manager.c's draw_row_text()): a non-interactive
// text draw, same gt_window_draw_text_args+syscall 32 both of those
// already used. label_set_text() is the real reason this is a struct and
// not a bare free function - a caller like settings.c's redraw_stats()
// needs to update the same on-screen text repeatedly, not just draw it once.
typedef struct {
    int window_id;
    u32 x, y;
    char* text;
    u32 fg_color, bg_color;
} label;

static __attribute__((unused)) void label_draw(label* self) {
    gt_window_draw_text_args args;
    args.id = self->window_id;
    args.x = self->x;
    args.y = self->y;
    args.fg_color = self->fg_color;
    args.bg_color = self->bg_color;
    args.text = self->text;
    gt_syscall(32, (u64) &args, 0, 0);
}

static __attribute__((unused)) void label_init(label* self, int window_id, u32 x, u32 y,
                         char* text, u32 fg_color, u32 bg_color) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->text = text;
    self->fg_color = fg_color;
    self->bg_color = bg_color;
    label_draw(self);
}

// Updates the text and redraws immediately - callers own clearing any old
// text first if the new string is shorter (matches how every existing
// hand-rolled redraw in this codebase already handles this, e.g. settings.c's
// redraw_stats() fills its background rect before redrawing text into it).
static __attribute__((unused)) void label_set_text(label* self, char* text) {
    self->text = text;
    label_draw(self);
}

// Checkbox - a real second interaction model beyond Button's momentary
// press: a persistent toggled boolean, flipped once per real click.
// Mouse-hit-test/edge-detection shape is copied from button_poll() (same
// gt_window_query+gt_mouse_query calls, same self-pointer sanity guard) -
// this codebase's own established convention is per-widget copies, not a
// shared base type, since every widget here is a tiny, self-contained
// struct with no inheritance mechanism in C worth building for two types.
typedef struct {
    int window_id;
    u32 x, y, size;  // square box, body-local rect
    u32 box_color, check_color, bg_color;
    bool checked;
    bool was_down;
} checkbox;

static __attribute__((unused)) void checkbox_draw(checkbox* self) {
    gt_window_fill_rect_args outer;
    outer.id = self->window_id;
    outer.x = self->x;
    outer.y = self->y;
    outer.width = self->size;
    outer.height = self->size;
    outer.color = self->box_color;
    gt_syscall(30, (u64) &outer, 0, 0);

    // Inset by 2px on every side - an outline when unchecked (bg_color
    // inset over the box_color border), a solid fill when checked.
    u32 inset = self->size > 4 ? 2 : 0;
    gt_window_fill_rect_args inner;
    inner.id = self->window_id;
    inner.x = self->x + inset;
    inner.y = self->y + inset;
    inner.width = self->size - (inset * 2);
    inner.height = self->size - (inset * 2);
    inner.color = self->checked ? self->check_color : self->bg_color;
    gt_syscall(30, (u64) &inner, 0, 0);
}

static __attribute__((unused)) void checkbox_init(checkbox* self, int window_id, u32 x, u32 y, u32 size,
                            u32 box_color, u32 check_color, u32 bg_color, bool initial_checked) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->size = size;
    self->box_color = box_color;
    self->check_color = check_color;
    self->bg_color = bg_color;
    self->checked = initial_checked;
    self->was_down = false;
    checkbox_draw(self);
}

// Returns true exactly once per real click (down-transition while the
// cursor is inside) - same edge-detection shape as button_poll(), but
// flips self->checked internally FIRST and always redraws on a real
// click, since (unlike a Button) the visual state genuinely changed and
// must persist, not just reflect "currently held down."
static __attribute__((unused)) bool checkbox_poll(checkbox* self) {
    if ((u64) self < 0x80000000 || (u64) self > 0x80220000) {
        gt_debug_print("checkbox_poll: BAD self ptr 0x", (u64) self);
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
    bool inside = (u32) mouse_x >= screen_x && (u32) mouse_x < screen_x + self->size
        && (u32) mouse_y >= screen_y && (u32) mouse_y < screen_y + self->size;
    bool left_down = (buttons & 1) != 0;
    bool clicked = inside && left_down && !self->was_down;
    self->was_down = left_down;

    if (clicked) {
        self->checked = !self->checked;
        checkbox_draw(self);
    }
    return clicked;
}

// RadioButton - same click-edge-detection shape as checkbox, but
// exclusive within a group instead of independently toggled: every
// radio_button in a group shares one `int* selected` (the group's own
// current selection, -1 = none). No true circle primitive exists in this
// codebase (window_fill_rect - syscall 30 - only draws rectangles), so
// this renders as nested squares, same stated simplification checkbox
// already uses. Real radio semantics: a click always SELECTS this one
// (never deselects on a re-click of an already-selected radio) - the
// caller is responsible for redrawing the rest of the group on the same
// real click if a different radio was previously selected (this toolkit
// has no observer/event system - every widget's app owns its own redraw
// sequencing, same convention every existing widget here already follows).
typedef struct {
    int window_id;
    u32 x, y, size;
    u32 ring_color, dot_color, bg_color;
    int group_index;   // this radio's own index within its group
    int* selected;      // shared across the whole group
    bool was_down;
} radio_button;

static __attribute__((unused)) void radio_button_draw(radio_button* self) {
    gt_window_fill_rect_args outer;
    outer.id = self->window_id;
    outer.x = self->x;
    outer.y = self->y;
    outer.width = self->size;
    outer.height = self->size;
    outer.color = self->ring_color;
    gt_syscall(30, (u64) &outer, 0, 0);

    u32 inset = self->size > 4 ? 2 : 0;
    gt_window_fill_rect_args inner;
    inner.id = self->window_id;
    inner.x = self->x + inset;
    inner.y = self->y + inset;
    inner.width = self->size - (inset * 2);
    inner.height = self->size - (inset * 2);
    inner.color = (*self->selected == self->group_index) ? self->dot_color : self->bg_color;
    gt_syscall(30, (u64) &inner, 0, 0);
}

static __attribute__((unused)) void radio_button_init(radio_button* self, int window_id, u32 x, u32 y, u32 size,
                               u32 ring_color, u32 dot_color, u32 bg_color,
                               int group_index, int* selected) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->size = size;
    self->ring_color = ring_color;
    self->dot_color = dot_color;
    self->bg_color = bg_color;
    self->group_index = group_index;
    self->selected = selected;
    self->was_down = false;
    radio_button_draw(self);
}

static __attribute__((unused)) bool radio_button_poll(radio_button* self) {
    if ((u64) self < 0x80000000 || (u64) self > 0x80220000) {
        gt_debug_print("radio_button_poll: BAD self ptr 0x", (u64) self);
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
    bool inside = (u32) mouse_x >= screen_x && (u32) mouse_x < screen_x + self->size
        && (u32) mouse_y >= screen_y && (u32) mouse_y < screen_y + self->size;
    bool left_down = (buttons & 1) != 0;
    bool clicked = inside && left_down && !self->was_down;
    self->was_down = left_down;

    if (clicked) {
        *self->selected = self->group_index;
        radio_button_draw(self);
    }
    return clicked;
}

// ProgressBar - non-interactive, no poll function: bg_color fills the
// full width, fill_color overlays a width scaled to percent (0-100,
// clamped). Same two-rect technique as checkbox/radio_button, just no
// mouse/click handling at all.
typedef struct {
    int window_id;
    u32 x, y, width, height;
    u32 bg_color, fill_color;
    u32 percent;  // 0-100
} progress_bar;

static __attribute__((unused)) void progress_bar_draw(progress_bar* self) {
    gt_window_fill_rect_args bg;
    bg.id = self->window_id;
    bg.x = self->x;
    bg.y = self->y;
    bg.width = self->width;
    bg.height = self->height;
    bg.color = self->bg_color;
    gt_syscall(30, (u64) &bg, 0, 0);

    u32 fill_width = (self->width * self->percent) / 100;
    if (fill_width > 0) {
        gt_window_fill_rect_args fill;
        fill.id = self->window_id;
        fill.x = self->x;
        fill.y = self->y;
        fill.width = fill_width;
        fill.height = self->height;
        fill.color = self->fill_color;
        gt_syscall(30, (u64) &fill, 0, 0);
    }
}

static __attribute__((unused)) void progress_bar_init(progress_bar* self, int window_id, u32 x, u32 y,
                                u32 width, u32 height, u32 bg_color, u32 fill_color,
                                u32 initial_percent) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;
    self->bg_color = bg_color;
    self->fill_color = fill_color;
    self->percent = initial_percent > 100 ? 100 : initial_percent;
    progress_bar_draw(self);
}

static __attribute__((unused)) void progress_bar_set_percent(progress_bar* self, u32 percent) {
    self->percent = percent > 100 ? 100 : percent;
    progress_bar_draw(self);
}

// Slider - a genuinely new interaction model, unlike every widget above:
// continuous click-and-drag-to-scrub, not click-edge-detection. Every
// poll, if the left button is down AND the cursor is anywhere inside the
// track rect, the value is recomputed directly from the cursor's
// horizontal position - clicking anywhere on the track jumps the value
// there (real, common slider UX - e.g. a media player's scrub bar), then
// holding and moving keeps updating it. Returns true only when the value
// actually CHANGES this poll, not on every poll while held, so callers
// can react to real changes cheaply (same "only redraw/print on a real
// change" discipline checkbox_poll/radio_button_poll already follow).
typedef struct {
    int window_id;
    u32 x, y, width, height;  // track rect
    u32 track_color, handle_color;
    u32 min_value, max_value, value;
} slider;

#define SLIDER_HANDLE_WIDTH 6

static __attribute__((unused)) void slider_draw(slider* self) {
    gt_window_fill_rect_args track;
    track.id = self->window_id;
    track.x = self->x;
    track.y = self->y;
    track.width = self->width;
    track.height = self->height;
    track.color = self->track_color;
    gt_syscall(30, (u64) &track, 0, 0);

    u32 range = self->max_value - self->min_value;
    u32 usable_width = self->width > SLIDER_HANDLE_WIDTH ? self->width - SLIDER_HANDLE_WIDTH : 0;
    u32 handle_offset = range > 0 ? ((self->value - self->min_value) * usable_width) / range : 0;

    gt_window_fill_rect_args handle;
    handle.id = self->window_id;
    handle.x = self->x + handle_offset;
    handle.y = self->y;
    handle.width = SLIDER_HANDLE_WIDTH;
    handle.height = self->height;
    handle.color = self->handle_color;
    gt_syscall(30, (u64) &handle, 0, 0);
}

static __attribute__((unused)) void slider_init(slider* self, int window_id, u32 x, u32 y, u32 width, u32 height,
                          u32 track_color, u32 handle_color,
                          u32 min_value, u32 max_value, u32 initial_value) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->width = width;
    self->height = height;
    self->track_color = track_color;
    self->handle_color = handle_color;
    self->min_value = min_value;
    self->max_value = max_value;
    self->value = initial_value < min_value ? min_value : (initial_value > max_value ? max_value : initial_value);
    slider_draw(self);
}

static __attribute__((unused)) bool slider_poll(slider* self) {
    if ((u64) self < 0x80000000 || (u64) self > 0x80220000) {
        gt_debug_print("slider_poll: BAD self ptr 0x", (u64) self);
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
    if (!inside || !left_down) {
        return false;
    }

    u32 rel_x = (u32) mouse_x - screen_x;
    u32 range = self->max_value - self->min_value;
    u32 new_value = self->min_value + (rel_x * range) / self->width;
    if (new_value > self->max_value) {
        new_value = self->max_value;
    }
    if (new_value == self->value) {
        return false;
    }
    self->value = new_value;
    slider_draw(self);
    return true;
}

// ListView - real extraction of the row-select-with-highlight pattern
// already independently duplicated in proc/apps/file_manager/
// file_manager.c's redraw_all_rows()/g_row_btns[] and proc/apps/
// service_manager/service_manager.c's redraw_rows()/g_row_btns[]. This
// milestone adds the widget and demos it (ring3prog.c) - it deliberately
// does NOT retrofit either existing app to use it (real, separate,
// regression-risk-bearing follow-up, not bundled in here).
#define LIST_VIEW_MAX_ROWS 16

typedef struct {
    int window_id;
    u32 x, y, width, row_height;
    int row_count;
    char* labels[LIST_VIEW_MAX_ROWS];  // caller-owned strings, not copied - same convention label already uses
    int selected_index;  // -1 = none
    u32 row_color, selected_color, label_color, bg_color;
    bool was_down;  // click-edge-detection state, same field every other widget here carries
} list_view;

static __attribute__((unused)) void list_view_draw_row(list_view* self, int row) {
    u32 y = self->y + (u32) (row * (int) self->row_height);
    gt_window_fill_rect_args bg;
    bg.id = self->window_id;
    bg.x = self->x;
    bg.y = y;
    bg.width = self->width;
    bg.height = self->row_height;
    bg.color = (row < self->row_count)
        ? (row == self->selected_index ? self->selected_color : self->row_color)
        : self->bg_color;
    gt_syscall(30, (u64) &bg, 0, 0);

    if (row < self->row_count) {
        gt_window_draw_text_args text_args;
        text_args.id = self->window_id;
        text_args.x = self->x;
        text_args.y = y;
        text_args.fg_color = self->label_color;
        text_args.bg_color = bg.color;
        text_args.text = self->labels[row];
        gt_syscall(32, (u64) &text_args, 0, 0);
    }
}

static __attribute__((unused)) void list_view_init(list_view* self, int window_id, u32 x, u32 y,
                             u32 width, u32 row_height, u32 row_color,
                             u32 selected_color, u32 label_color, u32 bg_color) {
    self->window_id = window_id;
    self->x = x;
    self->y = y;
    self->width = width;
    self->row_height = row_height;
    self->row_count = 0;
    self->selected_index = -1;
    self->row_color = row_color;
    self->selected_color = selected_color;
    self->label_color = label_color;
    self->bg_color = bg_color;
    self->was_down = false;
}

// Replaces the row set and redraws everything - both the newly-populated
// rows AND any trailing rows that were populated before but aren't
// anymore, cleared to bg_color (same "clear stale slots" step file_manager.c's
// own redraw_all_rows() already does for its own row buttons).
static __attribute__((unused)) void list_view_set_rows(list_view* self, char** labels, int row_count) {
    self->row_count = row_count > LIST_VIEW_MAX_ROWS ? LIST_VIEW_MAX_ROWS : row_count;
    int i = 0;
    while (i < self->row_count) {
        self->labels[i] = labels[i];
        i = i + 1;
    }
    self->selected_index = -1;
    i = 0;
    while (i < LIST_VIEW_MAX_ROWS) {
        list_view_draw_row(self, i);
        i = i + 1;
    }
}

static __attribute__((unused)) int list_view_poll(list_view* self) {
    if ((u64) self < 0x80000000 || (u64) self > 0x80220000) {
        gt_debug_print("list_view_poll: BAD self ptr 0x", (u64) self);
        return -1;
    }
    i32 win_x, win_y;
    u32 win_width, win_height;
    if (!gt_window_query(self->window_id, &win_x, &win_y, &win_width, &win_height)) {
        return -1;
    }
    i32 mouse_x, mouse_y;
    u8 buttons;
    gt_mouse_query(&mouse_x, &mouse_y, &buttons);
    bool left_down = (buttons & 1) != 0;
    bool clicked_edge = left_down && !self->was_down;
    self->was_down = left_down;
    if (!clicked_edge) {
        return -1;
    }

    u32 screen_x = (u32) win_x + self->x;
    u32 screen_y = (u32) win_y + self->y;
    if ((u32) mouse_x < screen_x || (u32) mouse_x >= screen_x + self->width) {
        return -1;
    }
    int row = 0;
    while (row < self->row_count) {
        u32 row_y = screen_y + (u32) (row * (int) self->row_height);
        if ((u32) mouse_y >= row_y && (u32) mouse_y < row_y + self->row_height) {
            int old_selected = self->selected_index;
            self->selected_index = row;
            if (old_selected >= 0 && old_selected != row) {
                list_view_draw_row(self, old_selected);
            }
            list_view_draw_row(self, row);
            return row;
        }
        row = row + 1;
    }
    return -1;
}
