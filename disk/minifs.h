#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

bool mkfs(void);
bool fs_write_file(const char* name, u8* data, u32 len);
int fs_read_file(const char* name, u8* out_buffer, u32 max_len);
void copy_name(char* dst, const char* src);

bool fs_superblock_info(u32* file_count_out);
// index is 0..15 (MAX_FILES); returns false for an unused slot.
bool fs_list_entry(int index, char* name_out, u32* size_out);

#define MINIFS_MAX_FILES 16

#pragma GCC visibility pop
