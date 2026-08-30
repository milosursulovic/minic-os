#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real frame-backed shared memory - two (or more) processes mapping the
// SAME physical frames into their own address spaces via
// kernel/mm/paging/paging.h's map_page_in(), the same mechanism
// clone_address_space() already relies on for per-process isolation,
// just deliberately pointed at one shared frame instead of a private
// one. Backs syscalls 55-57.
#define SHM_SLOTS 4
#define SHM_MAX_PAGES 4  // 16KB cap per region - generous for a demo

typedef struct {
    bool used;
    u32 page_count;
    void* frames[SHM_MAX_PAGES];
} shared_region;

extern shared_region g_shared_regions[SHM_SLOTS];

// page_count = ceil(size/4096), capped at SHM_MAX_PAGES. Allocates that
// many real physical frames (kernel/mm/frames/frames.h's alloc_frame()).
// Returns a slot index, or -1 (out of slots, or size exceeds the cap).
int alloc_shared_memory(u32 size);
// Maps every frame into the given address space (any process's cr3, not
// just the caller's own - that's what makes mapping into a just-spawned
// child possible) at vaddr, vaddr+4096, ... - writable+user (0x06, the
// same flag value proc/process.c's spawn_process() already uses).
bool shared_memory_map(int index, u64 cr3, u64 vaddr);

#pragma GCC visibility pop
