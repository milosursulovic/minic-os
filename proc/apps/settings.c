// System Settings (Faza II point 25) - a real GUI app with two sections:
// Display (change + persist the desktop wallpaper color) and System Info
// (live, read-only stats: uptime, memory, disk usage). Auto-spawned by
// kmain.c at boot (Mechanism A, same as desktop_shell.c/terminal.c/
// file_manager.c).
//
// Deliberate, honest limitation: clicking a color swatch writes the new
// color to /system/settings.cfg, but does NOT change the wallpaper that's
// already on screen - there's no IPC/pub-sub mechanism in this kernel for
// one ring3 process to tell another ("desktop_shell, re-read your
// config") that a setting changed, so the new color only takes effect on
// the next boot, exactly like editing a real config file. A static label
// says so on the panel, rather than silently doing nothing with no
// explanation.
//
// _start must be at offset 0 - see ring3prog.c's own comment on this;
// same __attribute__((section(".text.start"))) + ring3.ld requirement.

#include "../../types.h"
#include "../gui_toolkit.h"

#define WINDOW_X 620
#define WINDOW_Y 60
#define WINDOW_WIDTH 170
#define WINDOW_HEIGHT 500
#define BODY_WIDTH WINDOW_WIDTH

#define BODY_COLOR 0x00000000u
#define TITLE_COLOR 0x00303030u
#define TEXT_COLOR 0x00FFFFFFu
#define HINT_COLOR 0x00888888u
#define SWATCH_LABEL_COLOR 0x00FFFFFFu

#define SETTINGS_PATH "/system/settings.cfg"
// Same file, relative to MiniFS root (no /system/ prefix) - syscalls
// 37-39 (fs_list/fs_delete/fs_mkdir) bypass VFS's prefix routing and
// call MiniFS directly, unlike vfs_write/vfs_read (syscalls 4/5).
#define SETTINGS_RELATIVE_PATH "settings.cfg"

// Same tick-delta throttle as desktop_shell.c's uptime label - redraws
// the whole stats block unconditionally once the interval passes, never
// on every poll (CLAUDE.md's documented QEMU/TCG-runs-fast-than-100Hz
// gotcha makes "redraw on value change" alone an ineffective throttle).
#define STATS_REDRAW_TICK_INTERVAL 50

static button g_swatch_navy;
static button g_swatch_green;
static button g_swatch_maroon;
static button g_swatch_gray;

// The four preset colors a user can pick - no color-picker widget exists
// in this toolkit (no sliders/numeric input yet), so presets are the
// only realistic option, same spirit as File Manager's canned
// auto-incrementing names.
#define COLOR_NAVY 0x001A3A5Cu    // this kernel's existing default
#define COLOR_GREEN 0x00203A1Cu
#define COLOR_MAROON 0x003A1C1Cu
#define COLOR_GRAY 0x002A2A2Au

// fs_write_file (kernel/fs/minifs.c) fails if the name already exists - a
// real bug hit here in practice: the first color pick creates
// settings.cfg fine, but every pick after that silently no-ops, leaving
// the old color in place forever. Delete-then-recreate is this
// filesystem's only "overwrite" - no in-place truncate/rewrite exists
// (same "no defrag, no reclaim" simplicity as its bump allocator).
// gt_fs_delete's failure (e.g. no prior file yet, on the very first
// pick) is expected and ignored.
static void write_wallpaper_color(u32 color) {
    gt_fs_delete(SETTINGS_RELATIVE_PATH);
    gt_vfs_write(SETTINGS_PATH, (u8*) &color, sizeof(color));
}

static void draw_static_label(int window_id, u32 x, u32 y, const char* text) {
    gt_window_draw_text_args args;
    args.id = window_id;
    args.x = x;
    args.y = y;
    args.fg_color = TEXT_COLOR;
    args.bg_color = BODY_COLOR;
    args.text = (char*) text;
    gt_syscall(32, (u64) &args, 0, 0);
}

// Redraws the whole System Info block - three rows, one label each. No
// per-row dirty tracking (unlike terminal.c's scrollback mirror): this is
// three cheap text draws behind a real tick-delta gate, not an
// unthrottled per-character stream, so the redraw-storm bug class this
// session hit twice doesn't apply the same way.
static void redraw_stats(int window_id, u32 y) {
    u32 total_frames, free_frames, disk_file_count;
    gt_sys_info(&total_frames, &free_frames, &disk_file_count);
    u64 ticks = gt_get_ticks();

    gt_window_fill_rect_args bg;
    bg.id = window_id;
    bg.x = 0;
    bg.y = y;
    bg.width = BODY_WIDTH;
    bg.height = 42;
    bg.color = BODY_COLOR;
    gt_syscall(30, (u64) &bg, 0, 0);

    char line[40];
    int i;

    i = 0;
    line[i] = 'U'; i++; line[i] = 'P'; i++; line[i] = ' '; i++;
    line[i] = '0'; i++; line[i] = 'X'; i++;
    i += gt_format_hex(ticks, &line[i]);
    draw_static_label(window_id, 0, y, line);

    i = 0;
    line[i] = 'M'; i++; line[i] = 'E'; i++; line[i] = 'M'; i++; line[i] = ' '; i++;
    line[i] = '0'; i++; line[i] = 'X'; i++;
    i += gt_format_hex(free_frames, &line[i]);
    line[i] = '.'; i++; line[i] = '.'; i++;
    line[i] = '0'; i++; line[i] = 'X'; i++;
    i += gt_format_hex(total_frames, &line[i]);
    draw_static_label(window_id, 0, y + 14, line);

    i = 0;
    line[i] = 'F'; i++; line[i] = 'I'; i++; line[i] = 'L'; i++; line[i] = 'E'; i++; line[i] = 'S'; i++;
    line[i] = ' '; i++; line[i] = '0'; i++; line[i] = 'X'; i++;
    i += gt_format_hex(disk_file_count, &line[i]);
    draw_static_label(window_id, 0, y + 28, line);
}

__attribute__((section(".text.start")))
void _start(void) {
    int window_id = gt_window_create(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT,
                                      BODY_COLOR, TITLE_COLOR);

    draw_static_label(window_id, 0, 0, "DISPLAY");
    button_init(&g_swatch_navy, window_id, 0, 14, 150, 24, "NAVY",
                COLOR_NAVY, COLOR_NAVY, SWATCH_LABEL_COLOR);
    button_init(&g_swatch_green, window_id, 0, 42, 150, 24, "GREEN",
                COLOR_GREEN, COLOR_GREEN, SWATCH_LABEL_COLOR);
    button_init(&g_swatch_maroon, window_id, 0, 70, 150, 24, "MAROON",
                COLOR_MAROON, COLOR_MAROON, SWATCH_LABEL_COLOR);
    button_init(&g_swatch_gray, window_id, 0, 98, 150, 24, "GRAY",
                COLOR_GRAY, COLOR_GRAY, SWATCH_LABEL_COLOR);

    gt_window_draw_text_args hint_args;
    hint_args.id = window_id;
    hint_args.x = 0;
    hint_args.y = 126;
    hint_args.fg_color = HINT_COLOR;
    hint_args.bg_color = BODY_COLOR;
    hint_args.text = (char*) "REBOOT TO";
    gt_syscall(32, (u64) &hint_args, 0, 0);
    hint_args.y = 140;
    hint_args.text = (char*) "APPLY";
    gt_syscall(32, (u64) &hint_args, 0, 0);

    draw_static_label(window_id, 0, 168, "SYSTEM INFO");
    u32 stats_y = 182;
    redraw_stats(window_id, stats_y);

    u64 last_rendered_ticks = gt_get_ticks();

    for (;;) {
        if (button_poll(&g_swatch_navy)) {
            write_wallpaper_color(COLOR_NAVY);
        }
        if (button_poll(&g_swatch_green)) {
            write_wallpaper_color(COLOR_GREEN);
        }
        if (button_poll(&g_swatch_maroon)) {
            write_wallpaper_color(COLOR_MAROON);
        }
        if (button_poll(&g_swatch_gray)) {
            write_wallpaper_color(COLOR_GRAY);
        }

        u64 current_ticks = gt_get_ticks();
        if (current_ticks - last_rendered_ticks >= STATS_REDRAW_TICK_INTERVAL) {
            redraw_stats(window_id, stats_y);
            last_rendered_ticks = current_ticks;
        }
    }
}
