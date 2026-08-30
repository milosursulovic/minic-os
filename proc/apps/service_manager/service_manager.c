// Service Manager GUI app - a real, live viewer+controller over
// kernel/services/service_manager.c's registry and crash-restart
// supervisor (Faza I point 11), reached through the new syscalls 65-68
// (gt_service_list/start/stop/restart). Before this, the only way to
// reach it was the console shell's `service <start|stop|restart|status>
// <name>` command (shell/shell/shell.c) - this is the first ring3/GUI
// exposure of it, the real UI the roadmap's own "service start network /
// stop / restart / status" text asks for.
//
// Row/select/act pattern mirrors proc/apps/file_manager/file_manager.c's
// own row-button list + toolbar (g_row_btns[]/g_selected_*/ROW_SELECTED_
// COLOR) - one real difference: File Manager only redraws after its OWN
// write (it's the sole GUI-side writer of the tree it shows), but
// running/restart_count here change continuously in the BACKGROUND (the
// Milestone 7 supervisor worker task respawning services on its own
// schedule), independent of anything this app does - so the list also
// redraws on a tick-interval throttle, same as proc/apps/settings/
// settings.c's redraw_stats(). Selection is tracked by NAME, not row
// index, since that's the real lookup key service_start/stop/restart
// take (syscalls 66-68) - matches this app's own real interaction model
// rather than a row position that could shift as the list refreshes.
//
// service_manager.h is included only for its SERVICE_SLOTS constant -
// same "include a kernel header just for its constants" precedent
// file_manager.c already sets with minifs.h. This app never touches
// g_services directly - only gt_service_list/start/stop/restart.
//
// _start must be at offset 0 - see ring3prog.c's own comment on this;
// same __attribute__((section(".text.start"))) + ring3.ld requirement.

#include "../../../types.h"
#include "../../gui_toolkit.h"
#include "../../../kernel/services/service_manager.h"

#define WINDOW_X 200
#define WINDOW_Y 130
#define WINDOW_WIDTH 300
#define WINDOW_HEIGHT 200
#define BODY_WIDTH WINDOW_WIDTH

#define TOOLBAR_HEIGHT 18
#define ROWS_TOP TOOLBAR_HEIGHT
#define ROW_HEIGHT 14

#define BODY_COLOR 0x00000000u
#define TITLE_COLOR 0x00303030u
#define TEXT_COLOR 0x00FFFFFFu
#define ROW_COLOR 0x00101010u
#define ROW_SELECTED_COLOR 0x00405070u
#define TOOLBAR_BTN_COLOR 0x00303030u
#define TOOLBAR_BTN_PRESSED_COLOR 0x00505050u

// Same "gate on a real tick delta" reasoning as settings.c's
// STATS_REDRAW_TICK_INTERVAL - restart_count/running change on their own
// in the background, need periodic redraw independent of user action.
#define LIST_REDRAW_TICK_INTERVAL 50

static char g_selected_name[32];  // "" = no selection
static button g_row_btns[SERVICE_SLOTS];
static char g_row_labels[SERVICE_SLOTS][48];
static char g_row_names[SERVICE_SLOTS][32];  // real (non-uppercased) names for selection/actions
static int g_row_count;

static button g_start_btn, g_stop_btn, g_restart_btn;

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

static bool streq_local(const char* a, const char* b) {
    int i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return false;
        }
        i = i + 1;
    }
    return a[i] == b[i];
}

static void copy_str(char* dst, const char* src, int max_len) {
    int i = 0;
    while (src[i] != '\0' && i < max_len - 1) {
        dst[i] = src[i];
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

// Redraws every row - real slots first, unused visible rows cleared.
// Wholesale, not incremental (SERVICE_SLOTS=8 is cheap enough), same
// spirit as device_manager.c's own redraw_list(). Preserves the current
// selection highlight by NAME across refreshes (a service's own registry
// slot never gets reused/removed - only the processes it spawns do - so
// comparing by name is both correct and stable here).
static void redraw_rows(int window_id) {
    g_row_count = 0;
    int index = 0;
    while (index < SERVICE_SLOTS) {
        char raw_name[32];
        u32 flags;
        u32 restart_count;
        if (gt_service_list(index, raw_name, &flags, &restart_count)) {
            copy_str(g_row_names[g_row_count], raw_name, 32);

            char* label = &g_row_labels[g_row_count][0];
            int i = 0;
            char upper_name[32];
            uppercase_copy(upper_name, raw_name, 32);
            i = append_str(label, i, upper_name);
            i = append_str(label, i, " RUN=0X");
            label[i] = (char) ('0' + ((flags & 2) ? 1 : 0));
            i = i + 1;
            i = append_str(label, i, " RST=0X");
            i += gt_format_hex(restart_count, &label[i]);
            label[i] = '\0';

            u32 y = ROWS_TOP + (u32) (g_row_count * ROW_HEIGHT);
            bool selected = streq_local(raw_name, g_selected_name);
            g_row_btns[g_row_count].window_id = window_id;
            g_row_btns[g_row_count].x = 0;
            g_row_btns[g_row_count].y = y;
            g_row_btns[g_row_count].width = BODY_WIDTH;
            g_row_btns[g_row_count].height = ROW_HEIGHT;
            g_row_btns[g_row_count].label = label;
            g_row_btns[g_row_count].normal_color = selected ? ROW_SELECTED_COLOR : ROW_COLOR;
            g_row_btns[g_row_count].pressed_color = ROW_SELECTED_COLOR;
            g_row_btns[g_row_count].label_color = TEXT_COLOR;
            g_row_btns[g_row_count].was_down = false;
            button_draw(&g_row_btns[g_row_count], false);
            g_row_btns[g_row_count].last_rendered_pressed = false;

            g_row_count = g_row_count + 1;
        }
        index = index + 1;
    }
    int row = g_row_count;
    while (row < SERVICE_SLOTS) {
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

    g_selected_name[0] = '\0';

    button_init(&g_start_btn, window_id, 0, 0, 96, 16, "START",
                TOOLBAR_BTN_COLOR, TOOLBAR_BTN_PRESSED_COLOR, TEXT_COLOR);
    button_init(&g_stop_btn, window_id, 100, 0, 96, 16, "STOP",
                TOOLBAR_BTN_COLOR, TOOLBAR_BTN_PRESSED_COLOR, TEXT_COLOR);
    button_init(&g_restart_btn, window_id, 200, 0, 96, 16, "RESTART",
                TOOLBAR_BTN_COLOR, TOOLBAR_BTN_PRESSED_COLOR, TEXT_COLOR);

    redraw_rows(window_id);

    u64 last_rendered_ticks = gt_get_ticks();

    for (;;) {
        if (button_poll(&g_start_btn)) {
            if (g_selected_name[0] != '\0') {
                gt_service_start(g_selected_name);
                redraw_rows(window_id);
            }
        }
        if (button_poll(&g_stop_btn)) {
            if (g_selected_name[0] != '\0') {
                gt_service_stop(g_selected_name);
                redraw_rows(window_id);
            }
        }
        if (button_poll(&g_restart_btn)) {
            if (g_selected_name[0] != '\0') {
                gt_service_restart(g_selected_name);
                redraw_rows(window_id);
            }
        }

        int row = 0;
        while (row < g_row_count) {
            if (button_poll(&g_row_btns[row])) {
                copy_str(g_selected_name, g_row_names[row], 32);
                redraw_rows(window_id);
                break;
            }
            row = row + 1;
        }

        u64 current_ticks = gt_get_ticks();
        if (current_ticks - last_rendered_ticks >= LIST_REDRAW_TICK_INTERVAL) {
            redraw_rows(window_id);
            last_rendered_ticks = current_ticks;
        }
    }
}
