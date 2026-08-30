// Device Manager GUI app - a real, live viewer over kernel/drivers/
// device_manager/device_manager.c's registry (Faza I point 9), reached
// through the new syscall 64 (gt_device_list). Before this, the only way
// to see the registry at all was the console shell's `devices` command
// (shell/shell/shell.c) - this is the first ring3/GUI exposure of it.
//
// Read-only by design: the registry only ever gets populated as a side
// effect of a driver's own successful init (device_manager_register is
// called from mouse_init/pci_record_device/vbe_init/e1000_init, never
// from user action), so there is nothing for this app to write. A device
// can appear mid-session (e.g. the user runs `pci`/`mouse`/`nic` from
// the console shell while this window is open) - the list is redrawn on
// a tick-interval throttle, same pattern as proc/apps/settings/settings.c's
// redraw_stats(), so it's a live view, not a one-shot snapshot.
//
// device_manager.h is included only for its MAX_DEVICES/DEVICE_CATEGORY_*
// constants - same "include a kernel header just for its constants"
// precedent proc/apps/file_manager/file_manager.c already sets with
// minifs.h. This app never touches g_devices directly - only gt_device_list.
//
// _start must be at offset 0 - see ring3prog.c's own comment on this;
// same __attribute__((section(".text.start"))) + ring3.ld requirement.

#include "../../../types.h"
#include "../../gui_toolkit.h"
#include "../../../kernel/drivers/device_manager/device_manager.h"

#define WINDOW_X 160
#define WINDOW_Y 90
#define WINDOW_WIDTH 340
#define WINDOW_HEIGHT 260
#define BODY_WIDTH WINDOW_WIDTH

#define BODY_COLOR 0x00000000u
#define TITLE_COLOR 0x00303030u
#define TEXT_COLOR 0x00FFFFFFu

#define ROW_HEIGHT 14
#define ROWS_TOP 14

// Same reasoning as settings.c's own STATS_REDRAW_TICK_INTERVAL: gate on
// a real tick DELTA, not "redraw on change" - CLAUDE.md's documented
// QEMU/TCG-runs-faster-than-100Hz gotcha makes "changed since last poll"
// alone an ineffective throttle.
#define LIST_REDRAW_TICK_INTERVAL 50

static void draw_row_text(int window_id, u32 y, const char* text) {
    gt_window_draw_text_args args;
    args.id = window_id;
    args.x = 0;
    args.y = y;
    args.fg_color = TEXT_COLOR;
    args.bg_color = BODY_COLOR;
    args.text = (char*) text;
    gt_syscall(32, (u64) &args, 0, 0);
}

static void uppercase_copy(char* dst, const char* src, int max_len) {
    int i = 0;
    while (src[i] != '\0' && i < max_len - 1) {
        char c = src[i];
        if (c >= 'a' && c <= 'z') {
            c = (char) (c - 'a' + 'A');
        }
        dst[i] = c;
        i = i + 1;
    }
    dst[i] = '\0';
}

static int append_str(char* dst, int i, const char* s) {
    int j = 0;
    while (s[j] != '\0') {
        dst[i] = s[j];
        i = i + 1;
        j = j + 1;
    }
    return i;
}

// Redraws the whole list - real slots first (used, in registry order),
// unused visible rows cleared to background. Not incremental (no per-row
// dirty tracking, unlike terminal.c's scrollback mirror): MAX_DEVICES=16
// rows is cheap enough to redraw wholesale on the throttled interval.
static void redraw_list(int window_id) {
    int row = 0;
    int index = 0;
    while (index < MAX_DEVICES) {
        char raw_name[32];
        int category;
        u32 info;
        u32 y = ROWS_TOP + (u32) (row * ROW_HEIGHT);
        if (gt_device_list(index, raw_name, &category, &info)) {
            char line[64];
            int i = 0;
            char upper_name[32];
            uppercase_copy(upper_name, raw_name, 32);
            i = append_str(line, i, upper_name);
            if (category == DEVICE_CATEGORY_PCI) {
                i = append_str(line, i, " [PCI] 0X");
            } else if (category == DEVICE_CATEGORY_PLATFORM) {
                i = append_str(line, i, " [PLATFORM] 0X");
            } else if (category == DEVICE_CATEGORY_INPUT) {
                i = append_str(line, i, " [INPUT] 0X");
            } else {
                i = append_str(line, i, " [?] 0X");
            }
            i += gt_format_hex(info, &line[i]);
            line[i] = '\0';
            draw_row_text(window_id, y, line);
            row = row + 1;
        }
        index = index + 1;
    }
    while (row * ROW_HEIGHT < (int) (MAX_DEVICES * ROW_HEIGHT)) {
        u32 y = ROWS_TOP + (u32) (row * ROW_HEIGHT);
        gt_window_fill_rect_args bg;
        bg.id = window_id;
        bg.x = 0;
        bg.y = y;
        bg.width = BODY_WIDTH;
        bg.height = ROW_HEIGHT;
        bg.color = BODY_COLOR;
        gt_syscall(30, (u64) &bg, 0, 0);
        row = row + 1;
    }
}

__attribute__((section(".text.start")))
void _start(void) {
    int window_id = gt_window_create(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT,
                                      BODY_COLOR, TITLE_COLOR);

    draw_row_text(window_id, 0, "DEVICES");
    redraw_list(window_id);

    u64 last_rendered_ticks = gt_get_ticks();

    for (;;) {
        u64 current_ticks = gt_get_ticks();
        if (current_ticks - last_rendered_ticks >= LIST_REDRAW_TICK_INTERVAL) {
            redraw_list(window_id);
            last_rendered_ticks = current_ticks;
        }
    }
}
