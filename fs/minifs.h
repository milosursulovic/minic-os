#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

bool mkfs(void);
// path may contain '/' - every component but the last must already exist
// and be a directory (fs_create_dir). Fails if the final name already
// exists, same as always.
bool fs_write_file(const char* path, u8* data, u32 len);
int fs_read_file(const char* path, u8* out_buffer, u32 max_len);
// Flat removal - no check for (and no recursive delete of) a directory's
// children, same "no defrag, no reclaim" simplicity as the rest of this
// filesystem's allocator.
bool fs_delete_file(const char* path);
// Allocates one fresh (zeroed) directory sector via the superblock's
// next_free_lba bump allocator and links it into the parent directory.
bool fs_create_dir(const char* path);
void copy_name(char* dst, const char* src);

bool fs_superblock_info(u32* file_count_out);
// dir_path "" lists the root directory; any other value must resolve to
// an existing directory (walking every path component, including the
// last, as a directory - unlike fs_write_file/fs_read_file/
// fs_delete_file, where only the last component is the target).
// index is 0..15 (MINIFS_MAX_FILES) within that ONE directory's own
// sector - a per-directory-level cap, not a whole-filesystem one, since
// any entry can itself be a subdirectory with its own 16 slots. Returns
// false for an unused slot or an unresolvable dir_path.
bool fs_list_entry(const char* dir_path, int index, char* name_out, u32* size_out, bool* is_dir_out);
// Same path resolution as fs_list_entry's dir_path (every component,
// including the last, must be a directory) - exposed on its own since a
// caller like the shell's `cd` needs to validate a target before
// committing to it, without also needing to list/read/write it.
bool fs_dir_exists(const char* path);

#define MINIFS_MAX_FILES 16

#pragma GCC visibility pop
