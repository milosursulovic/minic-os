#include "vfs.h"
#include "../../fs/vfs/vfs.h"
#include "../../fs/minifs/minifs.h"
#include "../../sched/task.h"
#include "../../../proc/process.h"

typedef struct __attribute__((packed)) {
    char* dir_path;
    int index;
    char* name_out;
    u32* size_out;
    bool* is_dir_out;
} fs_list_args;

bool syscall_vfs(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    if (num == 4) {
        int caller_process = g_tasks[g_current_task].process_index;
        u8 caller_uid = caller_process < 0 ? 0 : g_processes[caller_process].uid;
        char* path = (char*) a1;
        u8* buf = (u8*) a2;
        int n = vfs_read(path, buf, (u32) a3, caller_uid);
        if (n < 0) {
            *result = (u64) -1;
            return true;
        }
        *result = (u64) n;
        return true;
    }
    if (num == 5) {
        int caller_process = g_tasks[g_current_task].process_index;
        u8 caller_uid = caller_process < 0 ? 0 : g_processes[caller_process].uid;
        char* path = (char*) a1;
        u8* buf = (u8*) a2;
        bool ok = vfs_write(path, buf, (u32) a3, caller_uid);
        if (!ok) {
            *result = (u64) -1;
            return true;
        }
        *result = a3;
        return true;
    }
    if (num == 37) {
        // Routed through the VFS now (was a direct fs_list_entry call) so
        // a caller navigating from dir_path == "" sees the real mount
        // table (system/devices/processes) as the true namespace root,
        // not just a raw MiniFS listing - see kernel/fs/vfs/vfs.c's
        // vfs_list_entry().
        fs_list_args* args = (fs_list_args*) a1;
        bool ok = vfs_list_entry(args->dir_path, args->index, args->name_out,
                                  args->size_out, args->is_dir_out);
        *result = (u64) ok;
        return true;
    }
    if (num == 38) {
        char* path = (char*) a1;
        bool ok = fs_delete_file(path);
        *result = (u64) ok;
        return true;
    }
    if (num == 39) {
        char* path = (char*) a1;
        bool ok = fs_create_dir(path);
        *result = (u64) ok;
        return true;
    }
    if (num == 50) {
        int caller_process = g_tasks[g_current_task].process_index;
        u8 caller_uid = caller_process < 0 ? 0 : g_processes[caller_process].uid;
        char* path = (char*) a1;
        bool ok = fs_set_owner(path, (u8) a2, caller_uid);
        *result = ok ? 0 : (u64) -1;
        return true;
    }
    if (num == 51) {
        int caller_process = g_tasks[g_current_task].process_index;
        u8 caller_uid = caller_process < 0 ? 0 : g_processes[caller_process].uid;
        char* path = (char*) a1;
        bool ok = fs_set_mode(path, (u8) a2, caller_uid);
        *result = ok ? 0 : (u64) -1;
        return true;
    }
    return false;
}
