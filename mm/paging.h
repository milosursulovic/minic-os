#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

// PTE bit 63; meaningful because boot.s enables EFER.NXE at boot.
#define PAGE_NX 0x8000000000000000ULL

extern u64 g_pml4_phys;

void read_pml4(void);
bool map_page_in(u64 pml4_phys, u64 vaddr, u64 paddr, u64 flags);
bool map_page(u64 vaddr, u64 paddr, u64 flags);
u64 translate_in(u64 pml4_phys, u64 vaddr);
u64 clone_address_space(void);
void free_address_space(u64 pml4_phys);
void load_cr3(u64 phys);
void set_tss_rsp0(u64 rsp0);

#pragma GCC visibility pop
