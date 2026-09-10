#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// PTE bit 63; meaningful because boot.s enables EFER.NXE at boot.
#define PAGE_NX 0x8000000000000000ULL

extern u64 g_pml4_phys;

void read_pml4(void);
bool map_page_in(u64 pml4_phys, u64 vaddr, u64 paddr, u64 flags);
bool map_page(u64 vaddr, u64 paddr, u64 flags);
u64 translate_in(u64 pml4_phys, u64 vaddr);
u64 clone_address_space(void);
// Real copy-on-write fork() (Faza I point 4, item 8) - walks parent's
// own private region (PDPT[2]+, same shape free_address_space()'s own
// walk already uses) and, for every present leaf, demotes the PARENT's
// own mapping to read-only AND maps the exact same frame read-only into
// child_pml4_phys at the same vaddr, registering it with
// kernel/mm/cow/cow.h's cow_track(). child_pml4_phys must already be a
// fresh clone_address_space() result (shared kernel/heap sub-tables,
// empty private region). Returns false only on a real allocation
// failure partway through (out of frames for a new intermediate table).
bool clone_address_space_cow(u64 parent_pml4_phys, u64 child_pml4_phys);
void free_address_space(u64 pml4_phys);
void load_cr3(u64 phys);
void set_tss_rsp0(u64 rsp0);

#pragma GCC visibility pop
