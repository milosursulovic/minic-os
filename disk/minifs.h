#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

bool mkfs(void);
bool fs_write_file(const char* name, u8* data, u32 len);
int fs_read_file(const char* name, u8* out_buffer, u32 max_len);
void copy_name(char* dst, const char* src);

// Real MiniFS metadata for `ls` - deliberately a small API rather than
// exposing the raw on-disk superblock/dir_entry layout to callers (a
// real encapsulation boundary C's separate translation units give for
// free, unlike the flat single-namespace `import` model the MiniC-era
// version of this kernel used, where shell.mc read minifs.mc's on-disk
// structs directly).
bool fs_superblock_info(u32* file_count_out);
// index is 0..15 (MAX_FILES); returns false for an unused slot.
bool fs_list_entry(int index, char* name_out, u32* size_out);

#define MINIFS_MAX_FILES 16

#pragma GCC visibility pop
