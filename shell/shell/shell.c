// Interactive shell over keyboard.c's line buffer. Command implementations
// live in commands/<domain>.c, one file per subsystem (memory/system/
// process/fs/hardware/service/gui/net/ring3_demo) - this file keeps only
// what's genuinely cross-cutting: history, tab-completion, the command
// dispatch chain, and the couple of small helpers (split_two_args) more
// than one domain needs. See shell_state.h for the one piece of state
// (g_shell_cwd) shared between shell_tab_complete() here and commands/fs.c.

#include "shell.h"
#include "shell_state.h"
#include "../../kernel/drivers/io/io.h"
#include "../../kernel/drivers/keyboard/keyboard.h"
#include "../../kernel/lib/strings.h"
#include "../../kernel/fs/minifs/minifs.h"
#include "../../kernel/fs/vfs/vfs.h"

#include "commands/memory.h"
#include "commands/system.h"
#include "commands/process.h"
#include "commands/fs.h"
#include "commands/hardware.h"
#include "commands/service.h"
#include "commands/gui.h"
#include "commands/net.h"
#include "commands/ring3_demo.h"

void print_prompt(void) {
    vga_print("> ");
    serial_print("> ");
}

#define HISTORY_MAX 16
static char g_history[HISTORY_MAX][128];
static int g_history_count;          // total real entries recorded, capped at HISTORY_MAX
static int g_history_next;           // ring buffer write slot
static int g_history_browse_index = -1;  // -1 = fresh line, 0 = most recent entry, 1 = one before that, ...

void shell_history_add(const char* line) {
    if (line[0] == '\0') {
        return;  // don't clutter history with a bare Enter
    }
    int i = 0;
    while (line[i] != '\0' && i < 127) {
        g_history[g_history_next][i] = line[i];
        i = i + 1;
    }
    g_history[g_history_next][i] = '\0';
    g_history_next = (g_history_next + 1) % HISTORY_MAX;
    if (g_history_count < HISTORY_MAX) {
        g_history_count = g_history_count + 1;
    }
    g_history_browse_index = -1;
}

// Erases the in-progress command line in place (real per-char backspace,
// same mirroring as isr.c's own Backspace) then retypes whatever
// g_history_browse_index now points at (or nothing, for -1).
static void shell_history_redraw(void) {
    // g_vga_cursor might be sitting mid-line (Left/Right arrow) rather than
    // at the true end of the current line - walk it out to the end first
    // (mirroring every step, so the GUI terminal's own tracked column
    // follows along), since the erase loop below assumes it's erasing
    // from there.
    while (g_line_cursor < g_line_len) {
        g_vga_cursor = g_vga_cursor + 1;
        g_line_cursor = g_line_cursor + 1;
        term_scrollback_cursor_right();
    }

    while (g_line_len > 0) {
        g_line_len = g_line_len - 1;
        g_vga_cursor = g_vga_cursor - 1;
        g_vga[g_vga_cursor].character = ' ';
        g_vga[g_vga_cursor].color = 0x0F;
        serial_putc('\b');
        serial_putc(' ');
        serial_putc('\b');
        term_scrollback_backspace();
    }
    vga_update_cursor(g_vga_cursor);

    if (g_history_browse_index >= 0) {
        int slot = ((g_history_next - 1 - g_history_browse_index) % HISTORY_MAX + HISTORY_MAX) % HISTORY_MAX;
        int i = 0;
        while (g_history[slot][i] != '\0') {
            char c = g_history[slot][i];
            g_line_buffer[g_line_len] = c;
            g_line_len = g_line_len + 1;
            vga_putc(c);
            serial_putc((u8) c);
            i = i + 1;
        }
    }
    g_line_cursor = g_line_len;  // recall always lands the cursor at the end, same as a real shell
}

void shell_history_up(void) {
    if (g_history_browse_index + 1 >= g_history_count) {
        return;  // already at the oldest entry (or history is empty)
    }
    g_history_browse_index = g_history_browse_index + 1;
    shell_history_redraw();
}

void shell_history_down(void) {
    if (g_history_browse_index < 0) {
        return;  // already on a fresh line, nothing to come back to
    }
    g_history_browse_index = g_history_browse_index - 1;
    shell_history_redraw();
}

// Splits "<first> <second>" (whatever follows a command's own prefix,
// e.g. args = &g_line_buffer[3] past "cp ") into two path fragments -
// shared by commands/fs.c's cp/mv and commands/service.c's cmd_service,
// the only commands here that take two arguments. No tokenizer exists in
// this codebase - same manual space-scan style as every other multi-word
// command. Returns false (usage error already printed) if there's no
// second argument.
bool split_two_args(char* args, char* first_out, char** second_out) {
    int space = 0;
    while (args[space] != '\0' && args[space] != ' ') {
        space = space + 1;
    }
    if (args[space] == '\0') {
        vga_print("usage: <cmd> <src> <dst>");
        serial_print("usage: <cmd> <src> <dst>");
        return false;
    }
    int i = 0;
    while (i < space) {
        first_out[i] = args[i];
        i = i + 1;
    }
    first_out[i] = '\0';
    *second_out = &args[space + 1];
    return true;
}

// Same ~70 command words cmd_help() prints below, minus the <addr>/<dir>/
// <src>/... placeholder tokens (those aren't real command names) - a
// third copy of the same list, but cmd_help() already keeps two literal
// copies (vga_print/serial_print) side by side, so this is consistent
// with, not a departure from, how this codebase already tolerates that
// duplication at this size.
//
// A static initializer can't fill this array directly - each element is
// one global (a string literal) address stored as another global's
// static data, which needs a real ELF64 relocation (R_X86_64_64) this
// kernel's ELF32 build container can't represent (same constraint
// kernel/gfx/cursor_image.h documents for g_cursor_image.pixels). Fixed
// the same way: assign every pointer at runtime instead (real `lea`/`mov`
// instructions, which -fPIC handles fine), lazily on first use.
#define SHELL_COMMAND_COUNT 94
static const char* g_shell_commands[SHELL_COMMAND_COUNT];
static bool g_shell_commands_initialized;

static void shell_commands_init(void) {
    if (g_shell_commands_initialized) {
        return;
    }
    g_shell_commands[0] = "help"; g_shell_commands[1] = "clear"; g_shell_commands[2] = "ticks";
    g_shell_commands[3] = "alloc"; g_shell_commands[4] = "bigalloc"; g_shell_commands[5] = "free";
    g_shell_commands[6] = "mem"; g_shell_commands[7] = "reset"; g_shell_commands[8] = "shutdown";
    g_shell_commands[9] = "reboot"; g_shell_commands[10] = "cursor"; g_shell_commands[11] = "frame";
    g_shell_commands[12] = "unframe"; g_shell_commands[13] = "frames"; g_shell_commands[14] = "map";
    g_shell_commands[15] = "tasks"; g_shell_commands[16] = "procs"; g_shell_commands[17] = "ps";
    g_shell_commands[18] = "objs"; g_shell_commands[19] = "netconns"; g_shell_commands[20] = "chan";
    g_shell_commands[21] = "send"; g_shell_commands[22] = "disk"; g_shell_commands[23] = "diskwrite";
    g_shell_commands[24] = "mkfs"; g_shell_commands[25] = "mkfile"; g_shell_commands[26] = "cat";
    g_shell_commands[27] = "ls"; g_shell_commands[28] = "pwd"; g_shell_commands[29] = "cd";
    g_shell_commands[30] = "mkdir"; g_shell_commands[31] = "cp"; g_shell_commands[32] = "mv";
    g_shell_commands[33] = "touch"; g_shell_commands[34] = "edit"; g_shell_commands[35] = "vfscat";
    g_shell_commands[36] = "vfswrite"; g_shell_commands[37] = "install"; g_shell_commands[38] = "spawn";
    g_shell_commands[39] = "ring3go"; g_shell_commands[40] = "ring3fault"; g_shell_commands[41] = "ring3nx";
    g_shell_commands[42] = "ring3reg"; g_shell_commands[43] = "ring3unreg"; g_shell_commands[44] = "ring3async";
    g_shell_commands[45] = "ring3asyncwrite"; g_shell_commands[46] = "ring3asyncping"; g_shell_commands[47] = "ring3asyncdns";
    g_shell_commands[48] = "ring3asynctcp"; g_shell_commands[49] = "ring3win"; g_shell_commands[50] = "ring3mouse";
    g_shell_commands[51] = "ring3text"; g_shell_commands[52] = "ring3button"; g_shell_commands[53] = "pci";
    g_shell_commands[54] = "nic"; g_shell_commands[55] = "fb"; g_shell_commands[56] = "text";
    g_shell_commands[57] = "mouse"; g_shell_commands[58] = "win"; g_shell_commands[59] = "winlist";
    g_shell_commands[60] = "wincontent"; g_shell_commands[61] = "textcontent"; g_shell_commands[62] = "buttoncontent";
    g_shell_commands[63] = "desktop"; g_shell_commands[64] = "arp"; g_shell_commands[65] = "ping";
    g_shell_commands[66] = "ipconfig"; g_shell_commands[67] = "dns"; g_shell_commands[68] = "tcp";
    g_shell_commands[69] = "echo"; g_shell_commands[70] = "pngtest";
    g_shell_commands[71] = "ring3fileobj"; g_shell_commands[72] = "ring3perms";
    g_shell_commands[73] = "ring3posix";
    g_shell_commands[74] = "ring3pipe"; g_shell_commands[75] = "ring3shm";
    g_shell_commands[76] = "devices"; g_shell_commands[77] = "exit";
    g_shell_commands[78] = "ring3tcpserver";
    g_shell_commands[79] = "service";
    g_shell_commands[80] = "ring3widgets"; g_shell_commands[81] = "checkboxcontent";
    g_shell_commands[82] = "radiocontent"; g_shell_commands[83] = "progresscontent";
    g_shell_commands[84] = "slidercontent"; g_shell_commands[85] = "listcontent";
    g_shell_commands[86] = "ring3focus";
    g_shell_commands[87] = "ring3thread";
    g_shell_commands[88] = "ring3sync";
    g_shell_commands[89] = "ring3shmsync";
    g_shell_commands[90] = "ring3msg";
    g_shell_commands[91] = "ring3objs";
    g_shell_commands[92] = "users";
    g_shell_commands[93] = "ring3users";
    g_shell_commands_initialized = true;
}

// Erases exactly `count` characters from the end of the in-progress line
// (a bounded version of shell_history_redraw()'s own erase loop - that
// one always clears the whole line, this only clears the word currently
// being completed) then retypes `text`.
static void tab_replace_word(int count, const char* text) {
    while (count > 0) {
        g_line_len = g_line_len - 1;
        g_vga_cursor = g_vga_cursor - 1;
        g_vga[g_vga_cursor].character = ' ';
        g_vga[g_vga_cursor].color = 0x0F;
        serial_putc('\b');
        serial_putc(' ');
        serial_putc('\b');
        term_scrollback_backspace();
        count = count - 1;
    }
    vga_update_cursor(g_vga_cursor);

    int i = 0;
    while (text[i] != '\0' && g_line_len < 127) {
        char c = text[i];
        g_line_buffer[g_line_len] = c;
        g_line_len = g_line_len + 1;
        vga_putc(c);
        serial_putc((u8) c);
        i = i + 1;
    }
}

// Tab-completion, driven by kernel/isr/isr.c on the Tab scancode (0x0F) -
// position-based, not command-aware: the first word completes against a
// fixed command-name table, anything after the first space completes
// against the current directory's own entries (fs_list_entry, same call
// commands/fs.c's cmd_ls already uses).
void shell_tab_complete(void) {
    shell_commands_init();

    // Completion always operates on the LAST word in the line (word_start
    // is computed from g_line_len below, not from wherever the cursor
    // happens to be) - if the cursor was moved mid-line (Left/Right
    // arrow), walk it back out to the end first, mirroring every step, so
    // tab_replace_word()'s own erase/retype (which assumes it's editing
    // right at the end) stays correct instead of silently editing the
    // wrong screen position.
    while (g_line_cursor < g_line_len) {
        g_vga_cursor = g_vga_cursor + 1;
        g_line_cursor = g_line_cursor + 1;
        term_scrollback_cursor_right();
    }

    // g_line_buffer is only null-terminated at g_line_len on Enter (isr.c) -
    // mid-typing it can hold stale trailing bytes from a previous, longer
    // command. Every string op below treats g_line_buffer as a real
    // C-string, so mark the real end here first (safe: isr.c already
    // guarantees g_line_len < 127 on every insert).
    g_line_buffer[g_line_len] = '\0';

    int word_start = 0;
    int i = g_line_len - 1;
    while (i >= 0) {
        if (g_line_buffer[i] == ' ') {
            word_start = i + 1;
            break;
        }
        i = i - 1;
    }
    int word_len = g_line_len - word_start;
    const char* word = &g_line_buffer[word_start];
    bool completing_command = (word_start == 0);

    // Collect matches - command names, or the current directory's own
    // entries for an argument (same vfs_list_entry loop cmd_ls already
    // uses). Names are copied into a local buffer since vfs_list_entry
    // hands back one entry at a time, not a stable pointer.
    char matches[MINIFS_MAX_FILES][20];
    int match_count = 0;

    if (completing_command) {
        int c = 0;
        while (c < SHELL_COMMAND_COUNT && match_count < MINIFS_MAX_FILES) {
            if (starts_with(g_shell_commands[c], word) && strlen_(g_shell_commands[c]) >= word_len) {
                int j = 0;
                while (g_shell_commands[c][j] != '\0' && j < 19) {
                    matches[match_count][j] = g_shell_commands[c][j];
                    j = j + 1;
                }
                matches[match_count][j] = '\0';
                match_count = match_count + 1;
            }
            c = c + 1;
        }
    } else {
        int idx = 0;
        while (idx < MINIFS_MAX_FILES) {
            char name[20];
            u32 size;
            bool is_dir;
            if (vfs_list_entry(g_shell_cwd, idx, name, &size, &is_dir)) {
                if (starts_with(name, word)) {
                    int j = 0;
                    while (name[j] != '\0' && j < 19) {
                        matches[match_count][j] = name[j];
                        j = j + 1;
                    }
                    matches[match_count][j] = '\0';
                    match_count = match_count + 1;
                }
            }
            idx = idx + 1;
        }
    }

    if (match_count == 0) {
        return;
    }

    // Longest common prefix across every match, starting from what's
    // already typed - completes as far as it unambiguously can even with
    // several matches (e.g. "ring3a" among the 5 ring3async* commands).
    int common_len = strlen_(matches[0]);
    int m = 1;
    while (m < match_count) {
        int len = 0;
        while (len < common_len && matches[m][len] == matches[0][len]) {
            len = len + 1;
        }
        common_len = len;
        m = m + 1;
    }

    if (common_len > word_len) {
        char prefix[20];
        int j = 0;
        while (j < common_len) {
            prefix[j] = matches[0][j];
            j = j + 1;
        }
        prefix[j] = '\0';
        tab_replace_word(word_len, prefix);
    }

    if (match_count == 1) {
        // Ready for the next argument (or Enter) - no special-casing a
        // directory match with a trailing '/', a deliberate simplification.
        if (g_line_len < 127) {
            g_line_buffer[g_line_len] = ' ';
            g_line_len = g_line_len + 1;
            vga_putc(' ');
            serial_putc(' ');
        }
        g_line_cursor = g_line_len;
        return;
    }

    // Ambiguous beyond the common prefix - list every candidate, then
    // reprint the prompt and the in-progress line exactly as it was,
    // same as a real shell's own ambiguous-Tab behavior.
    new_line();
    int k = 0;
    while (k < match_count) {
        vga_print(matches[k]);
        serial_print(matches[k]);
        vga_print(" ");
        serial_print(" ");
        k = k + 1;
    }
    new_line();
    print_prompt();
    int p = 0;
    while (p < g_line_len) {
        vga_putc(g_line_buffer[p]);
        serial_putc((u8) g_line_buffer[p]);
        p = p + 1;
    }
    g_line_cursor = g_line_len;
}

void run_command(void) {
    if (streq(g_line_buffer, "help")) {
        cmd_help();
    } else if (streq(g_line_buffer, "clear")) {
        cmd_clear();
    } else if (streq(g_line_buffer, "ticks")) {
        cmd_ticks();
    } else if (streq(g_line_buffer, "alloc")) {
        cmd_alloc();
    } else if (streq(g_line_buffer, "bigalloc")) {
        cmd_big_alloc();
    } else if (streq(g_line_buffer, "free")) {
        cmd_free();
    } else if (starts_with(g_line_buffer, "free ")) {
        cmd_free_addr();
    } else if (streq(g_line_buffer, "mem")) {
        cmd_mem();
    } else if (streq(g_line_buffer, "reset")) {
        cmd_reset();
    } else if (streq(g_line_buffer, "exit")) {
        cmd_exit();
    } else if (streq(g_line_buffer, "ring3tcpserver")) {
        cmd_ring3_tcp_server();
    } else if (streq(g_line_buffer, "ring3widgets")) {
        cmd_ring3_widgets();
    } else if (streq(g_line_buffer, "ring3focus")) {
        cmd_ring3_focus();
    } else if (streq(g_line_buffer, "ring3thread")) {
        cmd_ring3_thread();
    } else if (streq(g_line_buffer, "ring3sync")) {
        cmd_ring3_sync();
    } else if (streq(g_line_buffer, "ring3shmsync")) {
        cmd_ring3_shm_sync();
    } else if (streq(g_line_buffer, "ring3msg")) {
        cmd_ring3_msg();
    } else if (streq(g_line_buffer, "ring3objs")) {
        cmd_ring3_objs();
    } else if (streq(g_line_buffer, "users")) {
        cmd_users();
    } else if (streq(g_line_buffer, "ring3users")) {
        cmd_ring3_users();
    } else if (streq(g_line_buffer, "checkboxcontent")) {
        cmd_checkboxcontent();
    } else if (streq(g_line_buffer, "radiocontent")) {
        cmd_radiocontent();
    } else if (streq(g_line_buffer, "progresscontent")) {
        cmd_progresscontent();
    } else if (streq(g_line_buffer, "slidercontent")) {
        cmd_slidercontent();
    } else if (streq(g_line_buffer, "listcontent")) {
        cmd_listcontent();
    } else if (starts_with(g_line_buffer, "service ")) {
        cmd_service();
    } else if (streq(g_line_buffer, "shutdown")) {
        cmd_shutdown();
    } else if (streq(g_line_buffer, "reboot")) {
        cmd_reboot();
    } else if (streq(g_line_buffer, "cursor")) {
        cmd_cursor();
    } else if (streq(g_line_buffer, "frames")) {
        cmd_frames();
    } else if (streq(g_line_buffer, "frame")) {
        cmd_frame();
    } else if (streq(g_line_buffer, "unframe")) {
        cmd_unframe();
    } else if (streq(g_line_buffer, "map")) {
        cmd_map();
    } else if (streq(g_line_buffer, "tasks")) {
        cmd_tasks();
    } else if (streq(g_line_buffer, "procs")) {
        cmd_procs();
    } else if (streq(g_line_buffer, "chan")) {
        cmd_chan();
    } else if (streq(g_line_buffer, "send")) {
        cmd_send();
    } else if (streq(g_line_buffer, "ps")) {
        cmd_ps();
    } else if (streq(g_line_buffer, "objs")) {
        cmd_objs();
    } else if (streq(g_line_buffer, "netconns")) {
        cmd_netconns();
    } else if (streq(g_line_buffer, "disk")) {
        cmd_disk();
    } else if (streq(g_line_buffer, "diskwrite")) {
        cmd_disk_write();
    } else if (streq(g_line_buffer, "mkfs")) {
        cmd_mkfs();
    } else if (streq(g_line_buffer, "mkfile")) {
        cmd_mkfile();
    } else if (streq(g_line_buffer, "cat")) {
        cmd_cat();
    } else if (starts_with(g_line_buffer, "cat ")) {
        cmd_cat_path();
    } else if (streq(g_line_buffer, "ls")) {
        cmd_ls();
    } else if (streq(g_line_buffer, "pwd")) {
        cmd_pwd();
    } else if (streq(g_line_buffer, "cd")) {
        cmd_cd_root();
    } else if (starts_with(g_line_buffer, "cd ")) {
        cmd_cd();
    } else if (starts_with(g_line_buffer, "mkdir ")) {
        cmd_mkdir();
    } else if (starts_with(g_line_buffer, "cp ")) {
        cmd_cp();
    } else if (starts_with(g_line_buffer, "mv ")) {
        cmd_mv();
    } else if (starts_with(g_line_buffer, "touch ")) {
        cmd_touch();
    } else if (starts_with(g_line_buffer, "edit ")) {
        cmd_edit();
    } else if (starts_with(g_line_buffer, "vfscat ")) {
        cmd_vfs_cat();
    } else if (streq(g_line_buffer, "vfswrite")) {
        cmd_vfs_write();
    } else if (streq(g_line_buffer, "install")) {
        cmd_install();
    } else if (streq(g_line_buffer, "spawn")) {
        cmd_spawn();
    } else if (streq(g_line_buffer, "ring3go")) {
        cmd_ring3_go();
    } else if (streq(g_line_buffer, "ring3fault")) {
        cmd_ring3_fault();
    } else if (streq(g_line_buffer, "ring3nx")) {
        cmd_ring3_nx();
    } else if (streq(g_line_buffer, "ring3reg")) {
        cmd_ring3_register();
    } else if (streq(g_line_buffer, "ring3unreg")) {
        cmd_ring3_unregister();
    } else if (streq(g_line_buffer, "ring3async")) {
        cmd_ring3_async();
    } else if (streq(g_line_buffer, "ring3asyncwrite")) {
        cmd_ring3_async_write();
    } else if (streq(g_line_buffer, "ring3asyncping")) {
        cmd_ring3_async_ping();
    } else if (streq(g_line_buffer, "ring3asyncdns")) {
        cmd_ring3_async_dns();
    } else if (streq(g_line_buffer, "ring3asynctcp")) {
        cmd_ring3_async_tcp();
    } else if (streq(g_line_buffer, "ring3win")) {
        cmd_ring3_window();
    } else if (streq(g_line_buffer, "ring3mouse")) {
        cmd_ring3_mouse();
    } else if (streq(g_line_buffer, "ring3text")) {
        cmd_ring3_text();
    } else if (streq(g_line_buffer, "ring3fileobj")) {
        cmd_ring3_file_object();
    } else if (streq(g_line_buffer, "ring3perms")) {
        cmd_ring3_perms();
    } else if (streq(g_line_buffer, "ring3posix")) {
        cmd_ring3_posix();
    } else if (streq(g_line_buffer, "ring3pipe")) {
        cmd_ring3_pipe();
    } else if (streq(g_line_buffer, "ring3shm")) {
        cmd_ring3_shm();
    } else if (streq(g_line_buffer, "ring3button")) {
        cmd_ring3_button();
    } else if (streq(g_line_buffer, "pci")) {
        cmd_pci();
    } else if (streq(g_line_buffer, "devices")) {
        cmd_devices();
    } else if (streq(g_line_buffer, "nic")) {
        cmd_nic();
    } else if (streq(g_line_buffer, "fb")) {
        cmd_fb();
    } else if (streq(g_line_buffer, "text")) {
        cmd_text();
    } else if (streq(g_line_buffer, "mouse")) {
        cmd_mouse();
    } else if (streq(g_line_buffer, "win")) {
        cmd_win();
    } else if (streq(g_line_buffer, "winlist")) {
        cmd_winlist();
    } else if (streq(g_line_buffer, "wincontent")) {
        cmd_wincontent();
    } else if (streq(g_line_buffer, "textcontent")) {
        cmd_textcontent();
    } else if (streq(g_line_buffer, "buttoncontent")) {
        cmd_buttoncontent();
    } else if (streq(g_line_buffer, "desktop")) {
        cmd_desktop();
    } else if (streq(g_line_buffer, "arp")) {
        cmd_arp();
    } else if (starts_with(g_line_buffer, "ping ")) {
        cmd_ping();
    } else if (streq(g_line_buffer, "ipconfig")) {
        cmd_ipconfig();
    } else if (streq(g_line_buffer, "dns")) {
        cmd_dns();
    } else if (streq(g_line_buffer, "tcp")) {
        cmd_tcp();
    } else if (starts_with(g_line_buffer, "echo ")) {
        cmd_echo();
    } else if (streq(g_line_buffer, "pngtest")) {
        cmd_pngtest();
    } else if (g_line_len > 0) {
        vga_print("unknown command");
        serial_print("unknown command");
    }
}
