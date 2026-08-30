// The desktop shell (Faza II point 22): a full-width wallpaper, a
// full-width taskbar with a launcher button and a live uptime label.
// Auto-spawned by kmain.c at boot (Mechanism A - same as init.c/
// hello_service.c, a fixed blob linked into kernel.elf, not a
// runtime-registered service) - unlike ring3prog.c's on-demand demos,
// this one just runs forever from boot, no shell trigger needed.
//
// _start must be at offset 0 - see ring3prog.c's own comment on this;
// same __attribute__((section(".text.start"))) + ring3.ld requirement.

#include "../../../types.h"
#include "../../gui_toolkit.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define TASKBAR_HEIGHT 20

#define WALLPAPER_COLOR 0x001A3A5Cu
#define TASKBAR_COLOR 0x00303030u
#define LAUNCHER_NORMAL_COLOR 0x00505050u
#define LAUNCHER_PRESSED_COLOR 0x0000AA00u
#define LABEL_COLOR 0x00FFFFFFu

// MENU dropdown - a small popup window with one row per launchable app,
// created fresh each time it's opened and closed again on any selection
// (or on re-clicking MENU) - no "click elsewhere to dismiss" here, a
// small explicitly-scoped dropdown, not a full window-manager menu.
#define POPUP_WIDTH 100
#define POPUP_ITEM_HEIGHT 18
#define POPUP_ITEM_COUNT 5
#define POPUP_HEIGHT (POPUP_ITEM_HEIGHT * POPUP_ITEM_COUNT)
#define POPUP_X 4
#define POPUP_Y (SCREEN_HEIGHT - TASKBAR_HEIGHT - POPUP_HEIGHT)
#define POPUP_BG_COLOR 0x00404040u
#define POPUP_ITEM_NORMAL_COLOR 0x00505050u
#define POPUP_ITEM_PRESSED_COLOR 0x0000AA00u

// How many raw ticks must pass before the uptime label redraws again -
// NOT "redraw on any change". CLAUDE.md's own documented gotcha is that
// QEMU/TCG's PIT timer runs far faster than the nominal 100Hz, so
// g_tick_count can advance on nearly every loop iteration regardless of
// real wall-clock time - "only redraw when the value changed" barely
// throttles anything in practice, since it's almost always true. Gating
// on a real tick DELTA bounds the redraw rate independent of however
// fast TCG happens to be running the timer.
#define UPTIME_REDRAW_TICK_INTERVAL 50

// kernel/drivers/rtc/rtc.c reads the raw RTC value - QEMU's own RTC
// defaults to UTC, confirmed empirically (screendump'd clock matched host
// `date -u`). Real timezone/DST handling would need the RTC's date too
// (deliberately not read - this driver is time-only), so for now this is
// a single fixed offset for Europe/Belgrade - +2 (CEST, currently in
// effect) rather than the +1 CET standard-time offset, since hardcoding
// "whichever is correct right now" is more honest than hardcoding a
// number that's already wrong today. Will read an hour off during CET
// (~late Oct - late Mar) until this gets real DST-aware.
#define BELGRADE_UTC_OFFSET_HOURS 2

// Zero-padded 2-digit decimal ("09", not "9") - gt_format_hex/print_decimal
// don't zero-pad, needed for a real HH:MM:SS clock read-out.
static void format_2digit(u8 value, char* out) {
    out[0] = (char) ('0' + (value / 10) % 10);
    out[1] = (char) ('0' + value % 10);
}

// Zero-padded 4-digit decimal, for a year (2000-2099 - see rtc.h on why
// it's always exactly 4 digits, no wider range to worry about here).
static void format_4digit(u16 value, char* out) {
    out[0] = (char) ('0' + (value / 1000) % 10);
    out[1] = (char) ('0' + (value / 100) % 10);
    out[2] = (char) ('0' + (value / 10) % 10);
    out[3] = (char) ('0' + value % 10);
}

__attribute__((section(".text.start")))
void _start(void) {
    // Settings (proc/settings.c) persists a chosen wallpaper color to
    // /system/settings.cfg as a raw 4-byte color value - read it here at
    // boot, same "reads its own config once at startup" convention as
    // every other setting this kernel has. No settings.cfg (every disk
    // image predating Settings, or one that's never had a color chosen)
    // falls back to today's hardcoded default - fully backward-compatible.
    u32 wallpaper_color = WALLPAPER_COLOR;
    u32 saved_color;
    if (gt_vfs_read("/system/settings.cfg", (u8*) &saved_color, sizeof(saved_color)) == (int) sizeof(saved_color)) {
        wallpaper_color = saved_color;
    }

    gt_window_create_borderless(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, wallpaper_color);
    int taskbar_id = gt_window_create_borderless(0, SCREEN_HEIGHT - TASKBAR_HEIGHT,
                                                  SCREEN_WIDTH, TASKBAR_HEIGHT, TASKBAR_COLOR);

    // Paint the whole taskbar background first - once anything is drawn
    // into a window's content buffer (the button/label below), the flat
    // body_color stops applying and untouched cells default to 0 (black),
    // not TASKBAR_COLOR. Without this the taskbar would render as a black
    // bar with a gray button floating in it, not a solid-colored one.
    gt_window_fill_rect_args bg_args;
    bg_args.id = taskbar_id;
    bg_args.x = 0;
    bg_args.y = 0;
    bg_args.width = SCREEN_WIDTH;
    bg_args.height = TASKBAR_HEIGHT;
    bg_args.color = TASKBAR_COLOR;
    gt_syscall(30, (u64) &bg_args, 0, 0);

    button launcher;
    button_init(&launcher, taskbar_id, 4, 2, 60, 16, "MENU",  // font is uppercase-only
                LAUNCHER_NORMAL_COLOR, LAUNCHER_PRESSED_COLOR, LABEL_COLOR);

    int popup_id = -1;  // -1 = dropdown closed
    button popup_terminal, popup_files, popup_settings, popup_devices, popup_services;
    // Single-instance guards - no window-focus/bring-to-front concept
    // exists yet, so re-selecting an already-running app is a harmless
    // no-op instead of spawning a second window for it.
    bool terminal_open = false;
    bool files_open = false;
    bool settings_open = false;
    bool devices_open = false;
    bool services_open = false;

    // Sentinel: no real tick count is ever this value on a fresh boot, so
    // the first loop iteration always draws the label once.
    u64 last_rendered_ticks = (u64) -1;

    for (;;) {
        if (button_poll(&launcher)) {
            if (popup_id < 0) {
                popup_id = gt_window_create_borderless(POPUP_X, POPUP_Y, POPUP_WIDTH, POPUP_HEIGHT, POPUP_BG_COLOR);
                // Explicit full-rect fill first - once anything is drawn
                // into a window's content buffer, its flat body_color
                // stops applying to undrawn cells (same gotcha the
                // taskbar's own background hit above).
                gt_window_fill_rect_args popup_bg;
                popup_bg.id = popup_id;
                popup_bg.x = 0;
                popup_bg.y = 0;
                popup_bg.width = POPUP_WIDTH;
                popup_bg.height = POPUP_HEIGHT;
                popup_bg.color = POPUP_BG_COLOR;
                gt_syscall(30, (u64) &popup_bg, 0, 0);

                button_init(&popup_terminal, popup_id, 4, 0 * POPUP_ITEM_HEIGHT + 1,
                            POPUP_WIDTH - 8, POPUP_ITEM_HEIGHT - 2, "TERMINAL",
                            POPUP_ITEM_NORMAL_COLOR, POPUP_ITEM_PRESSED_COLOR, LABEL_COLOR);
                button_init(&popup_files, popup_id, 4, 1 * POPUP_ITEM_HEIGHT + 1,
                            POPUP_WIDTH - 8, POPUP_ITEM_HEIGHT - 2, "FILES",
                            POPUP_ITEM_NORMAL_COLOR, POPUP_ITEM_PRESSED_COLOR, LABEL_COLOR);
                button_init(&popup_settings, popup_id, 4, 2 * POPUP_ITEM_HEIGHT + 1,
                            POPUP_WIDTH - 8, POPUP_ITEM_HEIGHT - 2, "SETTINGS",
                            POPUP_ITEM_NORMAL_COLOR, POPUP_ITEM_PRESSED_COLOR, LABEL_COLOR);
                button_init(&popup_devices, popup_id, 4, 3 * POPUP_ITEM_HEIGHT + 1,
                            POPUP_WIDTH - 8, POPUP_ITEM_HEIGHT - 2, "DEVICES",
                            POPUP_ITEM_NORMAL_COLOR, POPUP_ITEM_PRESSED_COLOR, LABEL_COLOR);
                button_init(&popup_services, popup_id, 4, 4 * POPUP_ITEM_HEIGHT + 1,
                            POPUP_WIDTH - 8, POPUP_ITEM_HEIGHT - 2, "SERVICES",
                            POPUP_ITEM_NORMAL_COLOR, POPUP_ITEM_PRESSED_COLOR, LABEL_COLOR);
            } else {
                gt_window_close(popup_id);
                popup_id = -1;
            }
        }

        if (popup_id >= 0) {
            bool clicked_terminal = button_poll(&popup_terminal);
            bool clicked_files = !clicked_terminal && button_poll(&popup_files);
            bool clicked_settings = !clicked_terminal && !clicked_files && button_poll(&popup_settings);
            bool clicked_devices = !clicked_terminal && !clicked_files && !clicked_settings && button_poll(&popup_devices);
            bool clicked_services = !clicked_terminal && !clicked_files && !clicked_settings && !clicked_devices && button_poll(&popup_services);

            if (clicked_terminal) {
                if (!terminal_open) {
                    gt_spawn_app(0);
                    terminal_open = true;
                }
                gt_window_close(popup_id);
                popup_id = -1;
            } else if (clicked_files) {
                if (!files_open) {
                    gt_spawn_app(1);
                    files_open = true;
                }
                gt_window_close(popup_id);
                popup_id = -1;
            } else if (clicked_settings) {
                if (!settings_open) {
                    gt_spawn_app(2);
                    settings_open = true;
                }
                gt_window_close(popup_id);
                popup_id = -1;
            } else if (clicked_devices) {
                if (!devices_open) {
                    gt_spawn_app(3);
                    devices_open = true;
                }
                gt_window_close(popup_id);
                popup_id = -1;
            } else if (clicked_services) {
                if (!services_open) {
                    gt_spawn_app(4);
                    services_open = true;
                }
                gt_window_close(popup_id);
                popup_id = -1;
            }
        }

        // See UPTIME_REDRAW_TICK_INTERVAL above - unthrottled, this was
        // consuming enough CPU via the round-robin scheduler (each
        // redraw triggers a full compositor_redraw(), an 800x600
        // back-buffer blit, not cheap) to effectively starve other ring3
        // tasks (proc/terminal.c's own redraw loop in particular) of
        // real wall-clock progress, so the terminal window's mirrored
        // text never visibly updated in interactive use even though the
        // underlying pipeline worked.
        u64 current_ticks = gt_get_ticks();
        if (last_rendered_ticks == (u64) -1
            || current_ticks - last_rendered_ticks >= UPTIME_REDRAW_TICK_INTERVAL) {
            char uptime_text[24];
            int i = 0;
            uptime_text[i] = 'U'; i = i + 1;
            uptime_text[i] = 'P'; i = i + 1;
            uptime_text[i] = ' '; i = i + 1;
            uptime_text[i] = '0'; i = i + 1;
            uptime_text[i] = 'X'; i = i + 1;  // font only has uppercase - lowercase 'x' renders blank
            gt_format_hex(current_ticks, &uptime_text[i]);

            gt_window_draw_text_args label_args;
            label_args.id = taskbar_id;
            label_args.x = 700;
            label_args.y = 6;
            label_args.fg_color = LABEL_COLOR;
            label_args.bg_color = TASKBAR_COLOR;
            label_args.text = &uptime_text[0];
            gt_syscall(32, (u64) &label_args, 0, 0);

            // Real wall-clock time (kernel/drivers/rtc/rtc.c via syscall 42) -
            // redrawn on the exact same throttled cadence as uptime above,
            // not a second unthrottled poll path.
            u8 hour;
            u8 minute;
            u8 second;
            gt_get_time(&hour, &minute, &second);
            hour = (u8) ((hour + BELGRADE_UTC_OFFSET_HOURS) % 24);
            char clock_text[9];
            format_2digit(hour, &clock_text[0]);
            clock_text[2] = ':';
            format_2digit(minute, &clock_text[3]);
            clock_text[5] = ':';
            format_2digit(second, &clock_text[6]);
            clock_text[8] = '\0';

            gt_window_draw_text_args clock_args;
            clock_args.id = taskbar_id;
            clock_args.x = 620;
            clock_args.y = 6;
            clock_args.fg_color = LABEL_COLOR;
            clock_args.bg_color = TASKBAR_COLOR;
            clock_args.text = &clock_text[0];
            gt_syscall(32, (u64) &clock_args, 0, 0);

            // Real date (kernel/drivers/rtc/rtc.c via syscall 43) - DD.MM.YYYY,
            // the natural local format, not reformatted for the Belgrade
            // offset above (a real rollover near midnight would need
            // carrying the day/month/year forward, real calendar-math
            // complexity not worth it for this small a feature - a known,
            // deliberate simplification).
            u8 day;
            u8 month;
            u16 year;
            gt_get_date(&day, &month, &year);
            char date_text[11];
            format_2digit(day, &date_text[0]);
            date_text[2] = '.';
            format_2digit(month, &date_text[3]);
            date_text[5] = '.';
            format_4digit(year, &date_text[6]);
            date_text[10] = '\0';

            gt_window_draw_text_args date_args;
            date_args.id = taskbar_id;
            date_args.x = 520;
            date_args.y = 6;
            date_args.fg_color = LABEL_COLOR;
            date_args.bg_color = TASKBAR_COLOR;
            date_args.text = &date_text[0];
            gt_syscall(32, (u64) &date_args, 0, 0);

            last_rendered_ticks = current_ticks;
        }
    }
}
