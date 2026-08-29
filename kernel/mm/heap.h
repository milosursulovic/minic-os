#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

extern u64 g_heap_size;
extern u64 g_heap_cap;
extern void* g_last_alloc;

void heap_init(void);
bool heap_grow(u64 min_extra);
void* kalloc(u64 size);
void kfree(void* ptr);
u64 heap_free_bytes(void);

#pragma GCC visibility pop
