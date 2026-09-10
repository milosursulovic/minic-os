#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real, persistent directory-listing objects - backs syscalls 87-89
// (OBJ_DIRECTORY, proc/ipc/object/object.h). Faza I point 2 item 5.
// Unlike the older syscall 37 (vfs_list_entry), which takes a raw
// dir_path+index on every call with zero capability check, this goes
// through the same handle+rights system every other resource in this
// kernel already uses (mirrors proc/ipc/file/file.h's open_file shape).
#define DIRECTORY_SLOTS 8

typedef struct {
    bool used;
    char dir_path[128];
    int cursor;  // next index to hand to vfs_list_entry()
} open_directory;

extern open_directory g_open_directories[DIRECTORY_SLOTS];

// Copies+bounds dir_path, cursor starts at 0. Returns a slot index, or -1.
int directory_object_open(const char* dir_path);
// vfs_list_entry(dir_path, cursor, ...), advances cursor on success.
// Returns false at the real end of the listing (same convention
// vfs_list_entry's other existing callers already rely on).
bool directory_object_read_next(int slot, char* name_out, u32* size_out, bool* is_dir_out);
// Frees the slot - pure enumeration state, nothing to commit.
bool directory_object_close(int slot);

#pragma GCC visibility pop
