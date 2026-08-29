#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

int device_read(const char* name, u8* buf, u32 max_len);

#pragma GCC visibility pop
