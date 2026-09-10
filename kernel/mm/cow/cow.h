#pragma once
#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real copy-on-write bookkeeping (Faza I point 4, item 8) - a fork()'d
// child shares its parent's existing frames read-only until either side
// writes. Same "small fixed slots + linear scan" convention
// proc/ipc/shared_memory/shared_memory.c's own g_shared_regions[SHM_SLOTS]
// already established, not a giant per-physical-frame array (frames.c
// itself has zero per-frame metadata today).
#define COW_SLOTS 64

typedef struct {
    bool used;
    void* frame;
    int refcount;
} cow_entry;

extern cow_entry g_cow_frames[COW_SLOTS];

// Registers frame as newly COW-shared (refcount=2, parent+child) or
// increments an existing entry (a frame already COW from an earlier
// fork, shared again by a grandchild's own fork). Called once per frame
// per fork - see kernel/mm/paging/paging.h's clone_address_space_cow().
// Returns false only if the table is genuinely full.
bool cow_track(void* frame);
bool cow_is_shared(void* frame);
// Decrements frame's refcount. Returns true if the caller should now
// actually free_frame() it - either it just dropped to 0 (the last
// reference just went away), or it was never COW-tracked at all (a
// normal, exclusively-owned frame). Returns false if another reference
// remains - the caller must NOT free it.
bool cow_should_free(void* frame);

#pragma GCC visibility pop
