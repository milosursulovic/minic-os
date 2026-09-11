#pragma once
#include "core.h"
#include "channel.h"

// Ticks/time/date/term-scrollback/GUI-app-spawn wrappers - syscalls 35,
// 36, 40 (sys_info lives in vfs.h), 41. Split out of the former single
// gui_toolkit.h. time/date used to be syscalls 42/43 (direct kernel-side
// CMOS reads) - Faza I point 14 item 14 removed that path entirely and
// replaced it with real ring3 driver isolation (below).

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

// Real wall-clock time (always 24-hour) - Faza I point 14, item 14: no
// longer a direct kernel syscall into kernel/drivers/rtc/rtc.c. Instead
// a real request/response round trip to the isolated ring3 RTC driver
// (proc/drivers/rtc_driver/rtc_driver.c) over two Channels
// kmain.c wires up at spawn time - handle 1 = request (send), handle 2 =
// response (receive). Only meaningful for a process kmain.c actually
// granted those two handles to - currently only desktop_shell.c; a
// general any-process broker (per-consumer response channels, or a real
// connection protocol) is out of scope for this single-driver proof of
// concept.
#define RTC_REQUEST_HANDLE 1
#define RTC_RESPONSE_HANDLE 2

typedef struct {
    bool is_date;
    u8 v1;
    u8 v2;
    u16 v3;
} gt_rtc_response;

static __attribute__((unused)) void gt_get_time(u8* hour, u8* minute, u8* second) {
    u8 op = 0;
    gt_channel_send_msg(RTC_REQUEST_HANDLE, &op, 1);
    gt_rtc_response resp;
    gt_channel_receive_msg(RTC_RESPONSE_HANDLE, &resp, sizeof(resp));
    *hour = resp.v1;
    *minute = resp.v2;
    *second = (u8) resp.v3;
}

// Real date (day/month/year, year = 2000 + RTC's 2-digit year - no
// century register read) - same isolated-driver round trip as
// gt_get_time() above.
static __attribute__((unused)) void gt_get_date(u8* day, u8* month, u16* year) {
    u8 op = 1;
    gt_channel_send_msg(RTC_REQUEST_HANDLE, &op, 1);
    gt_rtc_response resp;
    gt_channel_receive_msg(RTC_RESPONSE_HANDLE, &resp, sizeof(resp));
    *day = resp.v1;
    *month = resp.v2;
    *year = resp.v3;
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
