// WiFi network connect GUI (Faza II point 5's GUI leg - the real-
// hardware driver arc item 5/5, Phase 8). A network list (real scan
// results, kernel/net/rtw89/dot11.c's dot11_scan via syscalls 101/102)
// + inline password field + Connect button - the Plasma/KDE-style
// shape the user asked for (list + password + connect, not a wizard).
//
// Real, honest limitation: no signal-strength bars (kernel/net/rtw89/
// dot11.h's own wifi_network has no RSSI field - this session's RX-
// descriptor research confirmed no such field exists in the real
// confirmed dwords, an honest omission rather than a fabricated
// number - see the plan file). Password entry echoes '*' rather than
// the real character (a local wrapper over text_box, which has no
// masking of its own) - real input/backspace logic is still
// text_box's, only the on-screen glyph is overridden.
//
// _start must be at offset 0 - see ring3prog.c's own comment on this;
// same __attribute__((section(".text.start"))) + ring3.ld requirement.

#include "../../../types.h"
#include "../../gui_toolkit.h"

#define WINDOW_X 300
#define WINDOW_Y 60
#define WINDOW_WIDTH 260
#define WINDOW_HEIGHT 300

#define BODY_COLOR 0x00000000u
#define TITLE_COLOR 0x00303030u
#define TEXT_COLOR 0x00FFFFFFu
#define HINT_COLOR 0x00888888u
#define ROW_COLOR 0x00202020u
#define ROW_SELECTED_COLOR 0x00405070u
#define ROW_LABEL_COLOR 0x00FFFFFFu

static button g_scan_button;
static button g_connect_button;
static list_view g_network_list;
static text_box g_password_box;

static wifi_network_info g_scan_results[WIFI_SCAN_MAX_NETWORKS];
static int g_scan_count;
static char g_row_label_buf[WIFI_SCAN_MAX_NETWORKS][40];
static char* g_row_label_ptrs[WIFI_SCAN_MAX_NETWORKS];

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

#define STATUS_Y 264
static void draw_status(int window_id, const char* text) {
    gt_window_fill_rect_args bg;
    bg.id = window_id;
    bg.x = 0;
    bg.y = STATUS_Y;
    bg.width = WINDOW_WIDTH;
    bg.height = 16;
    bg.color = BODY_COLOR;
    gt_syscall(30, (u64) &bg, 0, 0);
    draw_static_label(window_id, 0, STATUS_Y, text);
}

static void run_scan(int window_id) {
    draw_status(window_id, "SCANNING...");
    int handle = gt_wifi_scan_start();
    if (handle < 0) {
        draw_status(window_id, "NO WIFI CHIP");
        return;
    }
    g_scan_count = gt_wifi_scan_wait(handle, g_scan_results, WIFI_SCAN_MAX_NETWORKS);

    int i = 0;
    while (i < g_scan_count) {
        int p = 0;
        int j = 0;
        while (j < g_scan_results[i].ssid_len && p < 24) {
            g_row_label_buf[i][p] = g_scan_results[i].ssid[j];
            p = p + 1;
            j = j + 1;
        }
        g_row_label_buf[i][p] = ' ';
        p = p + 1;
        if (g_scan_results[i].has_rsn) {
            g_row_label_buf[i][p] = '['; p = p + 1;
            g_row_label_buf[i][p] = 'W'; p = p + 1;
            g_row_label_buf[i][p] = 'P'; p = p + 1;
            g_row_label_buf[i][p] = 'A'; p = p + 1;
            g_row_label_buf[i][p] = '2'; p = p + 1;
            g_row_label_buf[i][p] = ']'; p = p + 1;
        } else {
            g_row_label_buf[i][p] = '['; p = p + 1;
            g_row_label_buf[i][p] = 'O'; p = p + 1;
            g_row_label_buf[i][p] = 'P'; p = p + 1;
            g_row_label_buf[i][p] = 'E'; p = p + 1;
            g_row_label_buf[i][p] = 'N'; p = p + 1;
            g_row_label_buf[i][p] = ']'; p = p + 1;
        }
        g_row_label_buf[i][p] = 0;
        g_row_label_ptrs[i] = &g_row_label_buf[i][0];
        i = i + 1;
    }
    list_view_set_rows(&g_network_list, g_row_label_ptrs, g_scan_count);
    draw_status(window_id, g_scan_count > 0 ? "" : "NO NETWORKS FOUND");
}

// Redraws the password field's visible glyphs as '*' - real input/
// backspace/cursor logic all stays text_box's own, this only overrides
// what gets drawn afterward (text_box has no masking of its own).
static void mask_password_field(text_box* pw) {
    char masked[TEXT_BOX_MAX_LEN];
    int i = 0;
    while (i < pw->length) {
        masked[i] = '*';
        i = i + 1;
    }
    masked[i] = 0;
    gt_window_draw_text_args args;
    args.id = pw->window_id;
    args.x = pw->x + 4;
    args.y = pw->y + 3;
    args.fg_color = pw->fg_color;
    args.bg_color = pw->bg_color;
    args.text = masked;
    gt_syscall(32, (u64) &args, 0, 0);
}

static void run_connect(int window_id) {
    int idx = g_network_list.selected_index;
    if (idx < 0 || idx >= g_scan_count) {
        draw_status(window_id, "SELECT A NETWORK");
        return;
    }
    draw_status(window_id, "CONNECTING...");
    int handle = gt_wifi_connect_start(g_scan_results[idx].ssid, g_password_box.text,
                                        (u32) g_password_box.length);
    if (handle < 0) {
        draw_status(window_id, "CONNECT FAILED");
        return;
    }
    int state = gt_wifi_connect_wait(handle);
    if (state == WIFI_STATE_CONNECTED) {
        draw_status(window_id, "CONNECTED");
    } else if (state == WIFI_STATE_FAILED_WRONG_PASSWORD) {
        draw_status(window_id, "WRONG PASSWORD");
    } else if (state == WIFI_STATE_FAILED_AUTH) {
        draw_status(window_id, "AUTH FAILED");
    } else if (state == WIFI_STATE_FAILED_ASSOC) {
        draw_status(window_id, "ASSOC FAILED");
    } else {
        draw_status(window_id, "CONNECT FAILED");
    }
}

__attribute__((section(".text.start")))
void _start(void) {
    int window_id = gt_window_create(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT,
                                      BODY_COLOR, TITLE_COLOR);

    draw_static_label(window_id, 0, 0, "WIFI NETWORKS");
    button_init(&g_scan_button, window_id, 180, 0, 70, 16, "SCAN",
                0x00305030u, 0x00406040u, TEXT_COLOR);

    list_view_init(&g_network_list, window_id, 0, 20, WINDOW_WIDTH, 20,
                    ROW_COLOR, ROW_SELECTED_COLOR, ROW_LABEL_COLOR, BODY_COLOR);

    draw_static_label(window_id, 0, 224, "PASSWORD:");
    text_box_init(&g_password_box, window_id, 0, 238, 180, 20,
                  0x00101010u, TEXT_COLOR, 0x00505050u, "");
    button_init(&g_connect_button, window_id, 184, 238, 76, 20, "CONNECT",
                0x00305030u, 0x00406040u, TEXT_COLOR);

    draw_status(window_id, "CLICK SCAN");

    for (;;) {
        if (button_poll(&g_scan_button)) {
            run_scan(window_id);
        }
        list_view_poll(&g_network_list);
        if (text_box_poll(&g_password_box)) {
            mask_password_field(&g_password_box);
        }
        if (button_poll(&g_connect_button)) {
            run_connect(window_id);
        }
    }
}
