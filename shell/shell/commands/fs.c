#include "fs.h"
#include "../shell_state.h"
#include "../../editor/editor.h"
#include "../../../kernel/drivers/io/io.h"
#include "../../../kernel/drivers/keyboard/keyboard.h"
#include "../../../kernel/lib/strings.h"
#include "../../../kernel/fs/ata/ata.h"
#include "../../../kernel/fs/minifs/minifs.h"
#include "../../../kernel/fs/vfs/vfs.h"

void cmd_disk(void) {
    u8 buf[512];
    bool ok = ata_read_sector(1, buf);
    if (!ok) {
        vga_print("disk read failed");
        serial_print("disk read failed");
        return;
    }
    char* s = (char*) &buf[0];
    vga_print("sector 1: ");
    serial_print("sector 1: ");
    vga_print(s);
    serial_print(s);
}

void cmd_disk_write(void) {
    u8 write_buf[512];
    int i = 0;
    while (i < 512) {
        write_buf[i] = (u8) (i & 0xFF);
        i = i + 1;
    }
    bool wrote = ata_write_sector(100, write_buf);
    if (!wrote) {
        vga_print("disk write failed");
        serial_print("disk write failed");
        return;
    }
    u8 read_buf[512];
    bool read_ok = ata_read_sector(100, read_buf);
    if (!read_ok) {
        vga_print("disk write ok, readback failed");
        serial_print("disk write ok, readback failed");
        return;
    }
    bool match = true;
    i = 0;
    while (i < 512) {
        if (write_buf[i] != read_buf[i]) {
            match = false;
        }
        i = i + 1;
    }
    if (match) {
        vga_print("write+readback verified, 512/512 bytes match");
        serial_print("write+readback verified, 512/512 bytes match");
    } else {
        vga_print("MISMATCH - write or read is broken");
        serial_print("MISMATCH - write or read is broken");
    }
}

void cmd_mkfs(void) {
    bool ok = mkfs();
    if (ok) {
        vga_print("filesystem formatted");
        serial_print("filesystem formatted");
    } else {
        vga_print("mkfs failed");
        serial_print("mkfs failed");
    }
}

// Real VFS-absolute path ("" = the virtual root, "/system", "/system/sub",
// "/devices", "/processes", ...) - same convention
// proc/apps/file_manager.c's own current_path already uses, and the same
// real namespace it browses (this is genuinely the same mount table, not
// a parallel copy). File Manager's own GUI navigation (syscalls 37-39) is
// a separate call path but now resolves through the same VFS backend.
// Declared in shell_state.h - shell.c's own shell_tab_complete() also
// reads this for path-argument completion.
char g_shell_cwd[128] = "";

static int g_next_file_index;
static char g_last_file_name[128];  // full path, not just the bare name - cmd_cat needs no cwd logic of its own

// Faza I point 6 item 12: /apps and /users are now real, separate
// MiniFS-backed mounts too (not just /system), so the writability gate
// and raw-MiniFS path resolution below are generic (kernel/fs/vfs/vfs.c's
// vfs_is_writable()/vfs_resolve_minifs_path()) instead of hardcoding
// "starts_with(path, \"/system\")" - proc/apps/file_manager.c still has
// its own, narrower /system-only version (out of this item's scope; its
// GUI navigation wasn't part of the approved verification for this item).

// Creates a new file each call, inside the current directory: file0.mfs, file1.mfs, ...
void cmd_mkfile(void) {
    char name_buf[20];
    name_buf[0] = 'f'; name_buf[1] = 'i'; name_buf[2] = 'l'; name_buf[3] = 'e';
    name_buf[4] = (char) ('0' + (u8) (g_next_file_index % 10));
    name_buf[5] = '.'; name_buf[6] = 'm'; name_buf[7] = 'f'; name_buf[8] = 's';
    name_buf[9] = '\0';

    char content_buf[64];
    const char* prefix = "Hello from MiniFS, this is file #";
    int i = 0;
    while (prefix[i] != '\0') {
        content_buf[i] = prefix[i];
        i = i + 1;
    }
    content_buf[i] = (char) ('0' + (u8) (g_next_file_index % 10));
    i = i + 1;
    content_buf[i] = '\0';
    i = i + 1;

    char full_path[128];
    join_path(full_path, g_shell_cwd, name_buf);

    bool ok = vfs_write(full_path, (u8*) &content_buf[0], (u32) i, 0);  // shell acts as root
    if (!ok) {
        vga_print("mkfile failed");
        serial_print("mkfile failed");
        return;
    }
    copy_name(&g_last_file_name[0], full_path);
    g_next_file_index = g_next_file_index + 1;
    vga_print("created ");
    serial_print("created ");
    vga_print(name_buf);
    serial_print(name_buf);
}

void cmd_cat(void) {
    if (g_last_file_name[0] == '\0') {
        vga_print("no file yet - run mkfile first");
        serial_print("no file yet - run mkfile first");
        return;
    }
    u8 buf[65];
    int n = vfs_read(&g_last_file_name[0], buf, 64, 0);  // shell acts as root
    if (n < 0) {
        vga_print("cat failed");
        serial_print("cat failed");
        return;
    }
    buf[n] = 0;
    char* s = (char*) &buf[0];
    vga_print(s);
    serial_print(s);
}

void cmd_ls(void) {
    // file_count is a whole-MiniFS-volume stat (fs_superblock_info) - the
    // same number regardless of which MiniFS-backed mount (/system,
    // /apps, /users) it's shown under, since they're all the one real
    // disk; the virtual root, /devices/processes, /temp and /volumes
    // (none of them a real MiniFS volume) have no such concept.
    char minifs_scratch[200];
    if (vfs_resolve_minifs_path(g_shell_cwd, minifs_scratch)) {
        u32 file_count;
        if (!fs_superblock_info(&file_count)) {
            vga_print("ls failed - disk read error");
            serial_print("ls failed - disk read error");
            return;
        }
        vga_print("file_count: 0x");
        serial_print("file_count: 0x");
        print_hex((u64) file_count);
        vga_print("  ");
        serial_print("  ");
    }

    int i = 0;
    int shown = 0;
    while (i < MINIFS_MAX_FILES) {
        char name[20];
        u32 size;
        bool is_dir;
        if (vfs_list_entry(g_shell_cwd, i, name, &size, &is_dir)) {
            vga_print(name);
            serial_print(name);
            if (is_dir) {
                vga_print("/");
                serial_print("/");
            }
            vga_print(" 0x");
            serial_print(" 0x");
            print_hex((u64) size);
            vga_print("  ");
            serial_print("  ");
            shown = shown + 1;
        }
        i = i + 1;
    }
    if (shown == 0) {
        vga_print("(empty)");
        serial_print("(empty)");
    }
}

void cmd_pwd(void) {
    if (g_shell_cwd[0] == '\0') {
        vga_print("/");
        serial_print("/");
        return;
    }
    vga_print(g_shell_cwd);
    serial_print(g_shell_cwd);
}

// `cd` (no arg) or `cd /` -> root. `cd ..` -> strip the last component
// (same logic as proc/apps/file_manager.c's navigate_up, already correct
// for an absolute path: truncating "/system" at its last '/' correctly
// yields "", the virtual root). Otherwise resolve the arg against the
// current dir and only commit if a real vfs_list_entry() scan finds a
// same-named directory entry - this one check transparently covers
// entering a top-level mount from root, a real MiniFS subdirectory, and
// correctly refusing to "enter" a devfs/procfs pseudo-file (never
// is_dir), with no per-backend special-casing here. A failed cd leaves
// g_shell_cwd untouched, never half-updated.
void cmd_cd_root(void) {
    g_shell_cwd[0] = '\0';
}

void cmd_cd(void) {
    char* arg = &g_line_buffer[3];  // past "cd "
    if (streq(arg, "/")) {
        cmd_cd_root();
        return;
    }
    if (streq(arg, "..")) {
        int len = strlen_(g_shell_cwd);
        int i = len - 1;
        while (i >= 0 && g_shell_cwd[i] != '/') {
            i = i - 1;
        }
        if (i < 0) {
            g_shell_cwd[0] = '\0';
        } else {
            g_shell_cwd[i] = '\0';
        }
        return;
    }

    bool found = false;
    int idx = 0;
    while (idx < MINIFS_MAX_FILES) {
        char name[20];
        u32 size;
        bool is_dir;
        if (vfs_list_entry(g_shell_cwd, idx, name, &size, &is_dir)) {
            if (is_dir && streq(name, arg)) {
                found = true;
                break;
            }
        }
        idx = idx + 1;
    }
    if (!found) {
        vga_print("cd: no such directory");
        serial_print("cd: no such directory");
        return;
    }

    char new_path[128];
    join_path(new_path, g_shell_cwd, arg);
    int i = 0;
    while (new_path[i] != '\0' && i < 127) {
        g_shell_cwd[i] = new_path[i];
        i = i + 1;
    }
    g_shell_cwd[i] = '\0';
}

void cmd_mkdir(void) {
    char* arg = &g_line_buffer[6];  // past "mkdir "
    char new_path[128];
    join_path(new_path, g_shell_cwd, arg);
    bool ok = vfs_mkdir(new_path);
    if (!ok) {
        vga_print("mkdir failed");
        serial_print("mkdir failed");
        return;
    }
    vga_print("created ");
    serial_print("created ");
    vga_print(arg);
    serial_print(arg);
}

// cp <src> <dst>, both resolved against the current directory. Unlike
// mkfile's deliberate create-only demo semantics, a real `cp` is
// expected to overwrite an existing destination - MiniFS's
// fs_write_file refuses to write over an existing file (same limitation
// this session's Settings fix already hit for /system/settings.cfg), so
// delete-then-write is the only way to get real overwrite behavior.
void cmd_cp(void) {
    char src_name[64];
    char* dst_name;
    if (!split_two_args(&g_line_buffer[3], src_name, &dst_name)) {  // past "cp "
        return;
    }

    char src_path[128];
    char dst_path[128];
    join_path(src_path, g_shell_cwd, src_name);
    join_path(dst_path, g_shell_cwd, dst_name);

    u8 buf[4096];
    int n = vfs_read(src_path, buf, sizeof(buf), 0);  // shell acts as root
    if (n == -2) {
        vga_print("cp: source too large");
        serial_print("cp: source too large");
        return;
    }
    if (n < 0) {
        vga_print("cp: source not found");
        serial_print("cp: source not found");
        return;
    }
    if (!vfs_is_writable(dst_path)) {
        vga_print("cp: destination not writable");
        serial_print("cp: destination not writable");
        return;
    }
    vfs_delete(dst_path, 0);  // overwrite semantics - failure here just means dst didn't exist yet, expected
    bool ok = vfs_write(dst_path, buf, (u32) n, 0);  // shell acts as root
    if (!ok) {
        vga_print("cp failed");
        serial_print("cp failed");
        return;
    }
    vga_print("copied");
    serial_print("copied");
}

// mv <src> <dst> - MiniFS has no real in-place rename, so this is
// genuinely copy-then-delete-original, same honesty as every other
// documented limitation in this codebase, not hidden behind a
// misleadingly "atomic-sounding" name.
void cmd_mv(void) {
    char src_name[64];
    char* dst_name;
    if (!split_two_args(&g_line_buffer[3], src_name, &dst_name)) {  // past "mv "
        return;
    }

    char src_path[128];
    char dst_path[128];
    join_path(src_path, g_shell_cwd, src_name);
    join_path(dst_path, g_shell_cwd, dst_name);

    u8 buf[4096];
    int n = vfs_read(src_path, buf, sizeof(buf), 0);  // shell acts as root
    if (n == -2) {
        vga_print("mv: source too large");
        serial_print("mv: source too large");
        return;
    }
    if (n < 0) {
        vga_print("mv: source not found");
        serial_print("mv: source not found");
        return;
    }
    if (!vfs_is_writable(dst_path)) {
        vga_print("mv: destination not writable");
        serial_print("mv: destination not writable");
        return;
    }
    vfs_delete(dst_path, 0);  // overwrite semantics, same as cp
    bool ok = vfs_write(dst_path, buf, (u32) n, 0);  // shell acts as root
    if (!ok) {
        vga_print("mv failed");
        serial_print("mv failed");
        return;
    }
    vfs_delete(src_path, 0);  // best-effort - a non-writable/non-existent source is simply left alone
    vga_print("moved");
    serial_print("moved");
}

// touch <name> - creates an empty file. Unlike mkfile's deliberate
// create-only demo semantics, real touch never fails just because the
// file already exists (it would normally just update a timestamp there -
// no timestamp concept exists in this kernel at all, so an existing file
// is simply left alone and still reported as success).
void cmd_touch(void) {
    char* arg = &g_line_buffer[6];  // past "touch "
    char path[128];
    join_path(path, g_shell_cwd, arg);
    if (!vfs_is_writable(path)) {
        vga_print("touch: not writable here");
        serial_print("touch: not writable here");
        return;
    }
    u8 empty = 0;
    vfs_write(path, &empty, 0, 0);  // shell acts as root; ignored either way, same as before
    vga_print("touched ");
    serial_print("touched ");
    vga_print(arg);
    serial_print(arg);
}

// edit <name> - full-screen console text editor, see shell/editor.c. Takes
// over the whole display until Esc; nothing else runs on this task while
// g_editor_active is true (kmain.c's main loop skips its own prompt
// reprint, isr.c routes every keystroke to editor_handle_scancode()).
// editor.c itself stays completely VFS-unaware (raw MiniFS calls only) -
// gate here and pass it the stripped bare-relative path.
void cmd_edit(void) {
    char* arg = &g_line_buffer[5];  // past "edit "
    char path[128];
    join_path(path, g_shell_cwd, arg);
    char stripped[200];
    if (!vfs_resolve_minifs_path(path, stripped)) {
        vga_print("edit: not writable here");
        serial_print("edit: not writable here");
        return;
    }
    editor_start(stripped);
}

// cat <name> - reads an explicit path relative to the current directory,
// unlike bare `cat` (still kept, unchanged) which only ever replays
// whatever `mkfile` last created.
void cmd_cat_path(void) {
    char* arg = &g_line_buffer[4];  // past "cat "
    char path[128];
    join_path(path, g_shell_cwd, arg);
    u8 buf[65];
    int n = vfs_read(path, buf, 64, 0);  // shell acts as root
    if (n == -2) {
        vga_print("cat: file too large to display");
        serial_print("cat: file too large to display");
        return;
    }
    if (n < 0) {
        vga_print("cat: file not found");
        serial_print("cat: file not found");
        return;
    }
    buf[n] = 0;
    char* s = (char*) &buf[0];
    vga_print(s);
    serial_print(s);
}

void cmd_vfs_cat(void) {
    char* path = &g_line_buffer[7];  // past "vfscat "
    u8 buf[256];
    int n = vfs_read(path, buf, 256, 0);  // shell acts as root
    if (n == -2) {
        vga_print("vfscat: file too large to display");
        serial_print("vfscat: file too large to display");
        return;
    }
    if (n < 0) {
        vga_print("vfscat: not found");
        serial_print("vfscat: not found");
        return;
    }
    buf[n] = 0;
    char* s = (char*) &buf[0];
    vga_print(s);
    serial_print(s);
}

void cmd_vfs_write(void) {
    const char* content = "This file was written through the VFS layer, not MiniFS directly.";
    int len = strlen_(content) + 1;  // include the null terminator, same as mkfile's content
    bool ok = vfs_write("/system/vfsdemo.mfs", (u8*) content, (u32) len, 0);  // shell acts as root
    if (ok) {
        vga_print("wrote /system/vfsdemo.mfs via VFS");
        serial_print("wrote /system/vfsdemo.mfs via VFS");
    } else {
        vga_print("vfswrite failed");
        serial_print("vfswrite failed");
    }
}
