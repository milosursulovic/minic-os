// A small full-screen text editor (Faza II follow-up) - `edit <name>` takes
// over the whole VGA console until Esc, showing an existing file's content
// (if any) and letting you keep typing from there. No cursor-addressable
// movement back into already-typed text - the keyboard driver
// (kernel/drivers/keyboard.c) has no shift/ctrl/arrow-key handling at all,
// only lowercase letters/digits/space/enter, so real nano-style in-place
// editing is out of scope (a deliberate, user-confirmed limitation, not an
// oversight). What this still gives you: load a file, see it, keep writing
// real multi-line content, save it back - genuinely more than a bare
// append-only `cat >> file` since it does show you what's already there.

#include "editor.h"
#include "../shell/shell.h"
#include "../../kernel/drivers/io/io.h"
#include "../../kernel/drivers/keyboard/keyboard.h"
#include "../../kernel/fs/minifs/minifs.h"
#include "../../kernel/lib/strings.h"

#define EDITOR_MAX_SIZE 4096

bool g_editor_active;

static char g_editor_path[128];
static char g_editor_buffer[EDITOR_MAX_SIZE];
static int g_editor_len;
static int g_editor_col_len;  // chars typed on the CURRENT line only - bounds backspace

// vga_putc() itself has no bound check against the 80x25/2000-cell buffer
// (fine for the shell's own short, prompt-reset-every-command lines) - real
// multi-line file content can run the cursor past 2000 with no new_line()
// in between, which would corrupt whatever's next in memory. Called after
// every vga_putc() in this file only - the normal shell/backspace path
// doesn't hit this in practice, so it's left untouched.
static void editor_wrap_cursor(void) {
    if (g_vga_cursor >= 2000) {
        g_vga_cursor = 80;
    }
}

static void editor_clear_screen(bool keep_row0) {
    int i = keep_row0 ? 80 : 0;
    while (i < 2000) {
        g_vga[i].character = ' ';
        g_vga[i].color = 0x0F;
        i = i + 1;
    }
}

static void editor_save_and_exit(void) {
    fs_delete_file(g_editor_path);  // ok if it didn't exist yet - same delete-then-write overwrite pattern as cp/mv/settings
    fs_write_file(g_editor_path, (u8*) &g_editor_buffer[0], (u32) g_editor_len);

    g_editor_active = false;
    editor_clear_screen(true);
    g_vga_cursor = 80;
    vga_update_cursor(g_vga_cursor);

    vga_print("saved ");
    serial_print("saved ");
    vga_print(g_editor_path);
    serial_print(g_editor_path);
    vga_print(" (0x");
    serial_print(" (0x");
    print_hex((u64) g_editor_len);
    vga_print(" bytes)");
    serial_print(" bytes)");
    new_line();
    print_prompt();
}

void editor_start(const char* path) {
    u8 temp[EDITOR_MAX_SIZE];
    int n = fs_read_file(path, temp, EDITOR_MAX_SIZE);
    if (n == -2) {
        vga_print("edit: file too large");
        serial_print("edit: file too large");
        return;
    }

    int i = 0;
    while (path[i] != '\0' && i < 127) {
        g_editor_path[i] = path[i];
        i = i + 1;
    }
    g_editor_path[i] = '\0';

    editor_clear_screen(false);
    g_vga_cursor = 0;
    vga_print("EDIT MODE - ESC SAVES AND EXITS");
    serial_print("EDIT MODE - ESC SAVES AND EXITS");
    new_line();

    g_editor_len = 0;
    g_editor_col_len = 0;

    if (n >= 0) {
        int j = 0;
        while (j < n) {
            char c = (char) temp[j];
            if (c == '\n') {
                new_line();
                g_editor_col_len = 0;
            } else {
                vga_putc(c);
                editor_wrap_cursor();
                g_editor_col_len = g_editor_col_len + 1;
            }
            g_editor_buffer[g_editor_len] = c;
            g_editor_len = g_editor_len + 1;
            j = j + 1;
        }
    }

    g_editor_active = true;
}

void editor_handle_scancode(u8 scancode) {
    if (scancode == 0x01) {  // Esc - never mapped by init_scancode_table(), free to special-case
        editor_save_and_exit();
        return;
    }

    if (scancode == 0x0E) {  // Backspace - same-line-only, matching the shell's own convention
        if (g_editor_col_len > 0 && g_editor_len > 0) {
            g_editor_len = g_editor_len - 1;
            g_editor_col_len = g_editor_col_len - 1;
            g_vga_cursor = g_vga_cursor - 1;
            g_vga[g_vga_cursor].character = ' ';
            g_vga[g_vga_cursor].color = 0x0F;
            vga_update_cursor(g_vga_cursor);
            serial_putc('\b');
            serial_putc(' ');
            serial_putc('\b');
        }
        return;
    }

    char c = g_scancode_table[scancode];
    if (c == '\n') {
        if (g_editor_len < EDITOR_MAX_SIZE) {
            g_editor_buffer[g_editor_len] = '\n';
            g_editor_len = g_editor_len + 1;
        }
        new_line();
        g_editor_col_len = 0;
    } else if (c != '\0' && g_editor_len < EDITOR_MAX_SIZE) {
        g_editor_buffer[g_editor_len] = c;
        g_editor_len = g_editor_len + 1;
        vga_putc(c);
        editor_wrap_cursor();
        g_editor_col_len = g_editor_col_len + 1;
    }
}
