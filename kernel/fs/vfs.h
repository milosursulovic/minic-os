#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

extern const u32 BACKEND_MINIFS;
extern const u32 BACKEND_DEVICE;

bool vfs_mount(const char* prefix, u32 backend);
int vfs_read(const char* path, u8* buf, u32 max_len);
bool vfs_write(const char* path, u8* data, u32 len);

#pragma GCC visibility pop
