#pragma once
#include "core.h"

// Ticks/time/date/term-scrollback/GUI-app-spawn wrappers - syscalls 35,
// 36, 40 (sys_info lives in vfs.h), 41, 42, 43. Split out of the former
// single gui_toolkit.h.

typedef struct __attribute__((packed)) {
    u64 since_pos;
    char* out_buf;
    u32 max_len;
} gt_term_read_args;

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

// Spawns one of the fixed compiled-in GUI apps (0=terminal, 1=file_manager,
// 2=settings - kernel/syscall/handlers/system.c's own gui_app_bounds())
// instead of kmain.c auto-spawning all of them at boot. Returns the new
// process index, or -1 on failure (unknown app id, or the process/task
// table is full).
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
// comment in kernel/syscall/handlers/system.c).
static __attribute__((unused)) u32 gt_term_read(u64 since_pos, char* out_buf, u32 max_len) {
    gt_term_read_args args;
    args.since_pos = since_pos;
    args.out_buf = out_buf;
    args.max_len = max_len;
    return (u32) gt_syscall(36, (u64) &args, 0, 0);
}
