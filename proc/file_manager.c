// File Manager (Faza II point 24) - a real navigable GUI browser over
// the hierarchical MiniFS (disk/minifs.c). Auto-spawned by kmain.c at
// boot (Mechanism A, same as desktop_shell.c/terminal.c).
//
// Deliberately NOT polled on a tick interval: this program is the only
// GUI-side writer of the filesystem tree it displays (ignoring the rare
// case of the kernel shell also writing to MiniFS concurrently - an
// undocumented, accepted edge case, same spirit as every other scoped
// limitation in this codebase), so unlike terminal.c (which mirrors an
// externally, asynchronously-changing scrollback buffer and therefore
// needs unconditional polling + dirty-range redraw), every listing
// change here is something THIS program just did - refresh_listing()
// and its redraw only ever run right after a click that navigated,
// created, or deleted something. That is an even harder guarantee
// against the redraw-storm bug class already hit twice this session
// (desktop_shell.c's uptime label, terminal.c's row mirroring) than
// tick-interval throttling would be: zero redundant redraws, not just
// throttled ones.
//
// Row/entry names are uppercased for display only - font (gfx/font.h)
// has no lowercase glyphs, same technique as terminal.c.
//
// New File/New Dir use canned auto-incrementing names (FILEn.MFS /
// FOLDERn, single decimal digit mod 10, same convention as shell.c's
// cmd_mkfile) - no free-text input widget exists in this toolkit yet.
//
// _start must be at offset 0 - see ring3prog.c's own comment on this;
// same __attribute__((section(".text.start"))) + ring3.ld requirement.

#include "../types.h"
#include "gui_toolkit.h"
#include "../disk/minifs.h"

#define WINDOW_X 100
#define WINDOW_Y 390
#define WINDOW_WIDTH 500
#define WINDOW_HEIGHT 180
#define TITLEBAR_HEIGHT 20
#define BODY_WIDTH WINDOW_WIDTH
#define BODY_HEIGHT (WINDOW_HEIGHT - TITLEBAR_HEIGHT)

#define TOOLBAR_HEIGHT 18
#define PATH_LABEL_HEIGHT 14
#define ROWS_TOP (TOOLBAR_HEIGHT + PATH_LABEL_HEIGHT)
#define ROW_HEIGHT 14
#define VISIBLE_ROWS ((BODY_HEIGHT - ROWS_TOP) / ROW_HEIGHT)

#define BODY_COLOR 0x00000000u
#define TITLE_COLOR 0x00303030u
#define TEXT_COLOR 0x00FFFFFFu
#define ROW_COLOR 0x00101010u
#define ROW_SELECTED_COLOR 0x00405070u
#define TOOLBAR_BTN_COLOR 0x00303030u
#define TOOLBAR_BTN_PRESSED_COLOR 0x00505050u

#define MAX_PATH_LEN 128

static char g_current_path[MAX_PATH_LEN];

typedef struct {
    char name[20];
    u32 size;
    bool is_dir;
} entry;

static entry g_entries[MINIFS_MAX_FILES];
static int g_entry_count;
static int g_selected_index;  // index into g_entries, -1 = none

static button g_up_btn, g_newfile_btn, g_newdir_btn, g_delete_btn;
static button g_row_btns[VISIBLE_ROWS];
static char g_row_labels[VISIBLE_ROWS][32];
static char g_path_label[64];

static int g_next_file_num;
static int g_next_dir_num;

static int strlen_local(const char* s) {
    int n = 0;
    while (s[n] != '\0') {
        n = n + 1;
    }
    return n;
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

// path/name for a bare name at root, or path/name joined with '/'
// otherwise - the join both fs_* syscalls (relative to MiniFS root) and
// New File's vfs path (once /system/ is prefixed) need.
static void join_path(char* out, const char* base, const char* name) {
    int i = 0;
    if (base[0] != '\0') {
        while (base[i] != '\0') {
            out[i] = base[i];
            i = i + 1;
        }
        out[i] = '/';
        i = i + 1;
    }
    int j = 0;
    while (name[j] != '\0') {
        out[i] = name[j];
        i = i + 1;
        j = j + 1;
    }
    out[i] = '\0';
}

static void refresh_listing(void);
static void redraw_all_rows(int window_id);
static void redraw_path_label(int window_id);

static void navigate_into(int window_id, const char* dir_name) {
    char new_path[MAX_PATH_LEN];
    join_path(new_path, g_current_path, dir_name);
    int i = 0;
    while (new_path[i] != '\0' && i < MAX_PATH_LEN - 1) {
        g_current_path[i] = new_path[i];
        i = i + 1;
    }
    g_current_path[i] = '\0';
    g_selected_index = -1;
    refresh_listing();
    redraw_path_label(window_id);
    redraw_all_rows(window_id);
}

static void navigate_up(int window_id) {
    if (g_current_path[0] == '\0') {
        return;
    }
    int len = strlen_local(g_current_path);
    int i = len - 1;
    while (i >= 0 && g_current_path[i] != '/') {
        i = i - 1;
    }
    if (i < 0) {
        g_current_path[0] = '\0';
    } else {
        g_current_path[i] = '\0';
    }
    g_selected_index = -1;
    refresh_listing();
    redraw_path_label(window_id);
    redraw_all_rows(window_id);
}

// Keeps each entry's REAL (on-disk, possibly-lowercase) name - needed
// verbatim for navigate_into/do_delete's path resolution, since a name
// created by the kernel shell (e.g. cmd_mkfile's "file0.mfs") is
// lowercase on disk and an uppercased copy would fail to resolve.
// Uppercasing happens only where it's purely cosmetic - building each
// row's display label in redraw_all_rows.
static void refresh_listing(void) {
    g_entry_count = 0;
    int slot = 0;
    while (slot < (int) MINIFS_MAX_FILES && g_entry_count < VISIBLE_ROWS) {
        char name[20];
        u32 size;
        bool is_dir;
        if (gt_fs_list(g_current_path, slot, name, &size, &is_dir)) {
            int i = 0;
            while (name[i] != '\0' && i < 19) {
                g_entries[g_entry_count].name[i] = name[i];
                i = i + 1;
            }
            g_entries[g_entry_count].name[i] = '\0';
            g_entries[g_entry_count].size = size;
            g_entries[g_entry_count].is_dir = is_dir;
            g_entry_count = g_entry_count + 1;
        }
        slot = slot + 1;
    }
}

static void redraw_path_label(int window_id) {
    gt_window_fill_rect_args bg;
    bg.id = window_id;
    bg.x = 0;
    bg.y = TOOLBAR_HEIGHT;
    bg.width = BODY_WIDTH;
    bg.height = PATH_LABEL_HEIGHT;
    bg.color = BODY_COLOR;
    gt_syscall(30, (u64) &bg, 0, 0);

    const char* prefix = "PATH: ";
    int i = 0;
    while (prefix[i] != '\0') {
        g_path_label[i] = prefix[i];
        i = i + 1;
    }
    if (g_current_path[0] == '\0') {
        g_path_label[i] = 'R'; i = i + 1;
        g_path_label[i] = 'O'; i = i + 1;
        g_path_label[i] = 'O'; i = i + 1;
        g_path_label[i] = 'T'; i = i + 1;
        g_path_label[i] = '\0';
    } else {
        uppercase_copy(&g_path_label[i], g_current_path, 64 - i);
    }

    gt_window_draw_text_args text_args;
    text_args.id = window_id;
    text_args.x = 0;
    text_args.y = TOOLBAR_HEIGHT;
    text_args.fg_color = TEXT_COLOR;
    text_args.bg_color = BODY_COLOR;
    text_args.text = &g_path_label[0];
    gt_syscall(32, (u64) &text_args, 0, 0);
}

// Redraws every visible row slot - used rows get their entry's name/size
// text, unused slots get cleared to the background. Only called right
// after a listing-changing action (navigate/create/delete), never from
// an unconditional poll loop - see the file-level comment.
static void redraw_all_rows(int window_id) {
    int row = 0;
    while (row < VISIBLE_ROWS) {
        u32 y = ROWS_TOP + (u32) (row * ROW_HEIGHT);
        if (row < g_entry_count) {
            char* label = &g_row_labels[row][0];
            uppercase_copy(label, g_entries[row].name, 20);
            int i = strlen_local(label);
            if (g_entries[row].is_dir) {
                label[i] = '/';
                i = i + 1;
                label[i] = '\0';
            }

            g_row_btns[row].window_id = window_id;
            g_row_btns[row].x = 0;
            g_row_btns[row].y = y;
            g_row_btns[row].width = BODY_WIDTH;
            g_row_btns[row].height = ROW_HEIGHT;
            g_row_btns[row].label = label;
            g_row_btns[row].normal_color = (row == g_selected_index) ? ROW_SELECTED_COLOR : ROW_COLOR;
            g_row_btns[row].pressed_color = ROW_SELECTED_COLOR;
            g_row_btns[row].label_color = TEXT_COLOR;
            g_row_btns[row].was_down = false;
            button_draw(&g_row_btns[row], false);
            g_row_btns[row].last_rendered_pressed = false;
        } else {
            gt_window_fill_rect_args bg;
            bg.id = window_id;
            bg.x = 0;
            bg.y = y;
            bg.width = BODY_WIDTH;
            bg.height = ROW_HEIGHT;
            bg.color = BODY_COLOR;
            gt_syscall(30, (u64) &bg, 0, 0);
        }
        row = row + 1;
    }
}

static void do_new_file(int window_id) {
    int attempt = 0;
    while (attempt < 10) {
        char name[20];
        name[0] = 'F'; name[1] = 'I'; name[2] = 'L'; name[3] = 'E';
        name[4] = (char) ('0' + (u8) (g_next_file_num % 10));
        name[5] = '.'; name[6] = 'M'; name[7] = 'F'; name[8] = 'S';
        name[9] = '\0';
        g_next_file_num = g_next_file_num + 1;

        char rel_path[MAX_PATH_LEN];
        join_path(rel_path, g_current_path, name);
        char full_path[MAX_PATH_LEN + 8];
        full_path[0] = '/'; full_path[1] = 's'; full_path[2] = 'y'; full_path[3] = 's';
        full_path[4] = 't'; full_path[5] = 'e'; full_path[6] = 'm'; full_path[7] = '/';
        int i = 0;
        while (rel_path[i] != '\0') {
            full_path[8 + i] = rel_path[i];
            i = i + 1;
        }
        full_path[8 + i] = '\0';

        u8 content = 0;
        if (gt_vfs_write(full_path, &content, 1)) {
            g_selected_index = -1;
            refresh_listing();
            redraw_all_rows(window_id);
            return;
        }
        attempt = attempt + 1;
    }
}

static void do_new_dir(int window_id) {
    int attempt = 0;
    while (attempt < 10) {
        char name[20];
        name[0] = 'F'; name[1] = 'O'; name[2] = 'L'; name[3] = 'D'; name[4] = 'E'; name[5] = 'R';
        name[6] = (char) ('0' + (u8) (g_next_dir_num % 10));
        name[7] = '\0';
        g_next_dir_num = g_next_dir_num + 1;

        char rel_path[MAX_PATH_LEN];
        join_path(rel_path, g_current_path, name);
        if (gt_fs_mkdir(rel_path)) {
            g_selected_index = -1;
            refresh_listing();
            redraw_all_rows(window_id);
            return;
        }
        attempt = attempt + 1;
    }
}

static void do_delete(int window_id) {
    if (g_selected_index < 0 || g_selected_index >= g_entry_count) {
        return;
    }
    char rel_path[MAX_PATH_LEN];
    join_path(rel_path, g_current_path, g_entries[g_selected_index].name);
    gt_fs_delete(rel_path);
    g_selected_index = -1;
    refresh_listing();
    redraw_all_rows(window_id);
}

__attribute__((section(".text.start")))
void _start(void) {
    int window_id = gt_window_create(WINDOW_X, WINDOW_Y, WINDOW_WIDTH, WINDOW_HEIGHT,
                                      BODY_COLOR, TITLE_COLOR);

    g_current_path[0] = '\0';
    g_selected_index = -1;
    g_next_file_num = 0;
    g_next_dir_num = 0;

    button_init(&g_up_btn, window_id, 0, 0, 50, 16, "UP",
                TOOLBAR_BTN_COLOR, TOOLBAR_BTN_PRESSED_COLOR, TEXT_COLOR);
    button_init(&g_newfile_btn, window_id, 54, 0, 90, 16, "NEW FILE",
                TOOLBAR_BTN_COLOR, TOOLBAR_BTN_PRESSED_COLOR, TEXT_COLOR);
    button_init(&g_newdir_btn, window_id, 148, 0, 80, 16, "NEW DIR",
                TOOLBAR_BTN_COLOR, TOOLBAR_BTN_PRESSED_COLOR, TEXT_COLOR);
    button_init(&g_delete_btn, window_id, 232, 0, 70, 16, "DELETE",
                TOOLBAR_BTN_COLOR, TOOLBAR_BTN_PRESSED_COLOR, TEXT_COLOR);

    redraw_path_label(window_id);
    refresh_listing();
    redraw_all_rows(window_id);

    for (;;) {
        if (button_poll(&g_up_btn)) {
            navigate_up(window_id);
        }
        if (button_poll(&g_newfile_btn)) {
            do_new_file(window_id);
        }
        if (button_poll(&g_newdir_btn)) {
            do_new_dir(window_id);
        }
        if (button_poll(&g_delete_btn)) {
            do_delete(window_id);
        }
        int row = 0;
        while (row < g_entry_count) {
            if (button_poll(&g_row_btns[row])) {
                if (g_entries[row].is_dir) {
                    navigate_into(window_id, g_entries[row].name);
                } else if (g_selected_index != row) {
                    g_selected_index = row;
                    redraw_all_rows(window_id);
                }
            }
            row = row + 1;
        }
    }
}
