// PML4/PDPT/PD/PT walking and creation, using frames.c to supply table pages.
// All touched physical addresses are within boot.s's identity-mapped first
// 1GB, so tables can be edited via plain pointer dereference.
//
// Limitation: map_page() must not target a vaddr inside boot.s's static 1GB
// identity map (PDPT index 0) - those PD entries are 2MB huge pages (PS bit
// set), and walking past one as a PT pointer reads garbage.

#include "paging.h"
#include "../frames/frames.h"

u64 g_pml4_phys;

void read_pml4(void) {
    __asm__ volatile("mov %%cr3, %0" : "=r"(g_pml4_phys));
}

static void invalidate_page(u64 vaddr) {
    __asm__ volatile("invlpg (%0)" : : "r"(vaddr) : "memory");
}

static void zero_page(void* p) {
    u64* words = (u64*) p;
    u32 i = 0;
    while (i < 512) {
        words[i] = 0;
        i = i + 1;
    }
}

// Allocates and zeroes a fresh frame for this entry if not present yet.
static u64* table_get_or_create(u64* table, u32 index) {
    u64 entry = table[index];
    if ((entry & 1) == 0) {
        void* new_table = alloc_frame();
        if (new_table == NULL) {
            return NULL;
        }
        zero_page(new_table);
        // user bit here doesn't loosen protection - the CPU ANDs the user bit
        // across every level, so a supervisor-only leaf still blocks user access.
        table[index] = ((u64) new_table) | 0x07;  // present + writable + user
        return (u64*) new_table;
    }
    return (u64*) (entry & ~((u64) 0xFFF));
}

// Maps one 4KB page in an explicitly given address space, creating any
// missing PDPT/PD/PT tables. Works via plain pointer walk regardless of
// which CR3 is loaded, since table frames live in the shared identity map.
bool map_page_in(u64 pml4_phys, u64 vaddr, u64 paddr, u64 flags) {
    u32 i4 = (u32) ((vaddr >> 39) & 0x1FF);
    u32 i3 = (u32) ((vaddr >> 30) & 0x1FF);
    u32 i2 = (u32) ((vaddr >> 21) & 0x1FF);
    u32 i1 = (u32) ((vaddr >> 12) & 0x1FF);

    u64* pml4 = (u64*) pml4_phys;
    u64* pdpt = table_get_or_create(pml4, i4);
    if (pdpt == NULL) {
        return false;
    }
    u64* pd = table_get_or_create(pdpt, i3);
    if (pd == NULL) {
        return false;
    }
    u64* pt = table_get_or_create(pd, i2);
    if (pt == NULL) {
        return false;
    }

    pt[i1] = (paddr & ~((u64) 0xFFF)) | (flags & (((u64) 0xFFF) | PAGE_NX)) | 0x01;
    if (pml4_phys == g_pml4_phys) {
        invalidate_page(vaddr);  // only meaningful for the currently-loaded space
    }
    return true;
}

bool map_page(u64 vaddr, u64 paddr, u64 flags) {
    return map_page_in(g_pml4_phys, vaddr, paddr, flags);
}

// Returns the physical address vaddr resolves to, or 0 if any level isn't present.
u64 translate_in(u64 pml4_phys, u64 vaddr) {
    u32 i4 = (u32) ((vaddr >> 39) & 0x1FF);
    u32 i3 = (u32) ((vaddr >> 30) & 0x1FF);
    u32 i2 = (u32) ((vaddr >> 21) & 0x1FF);
    u32 i1 = (u32) ((vaddr >> 12) & 0x1FF);

    u64* pml4 = (u64*) pml4_phys;
    if ((pml4[i4] & 1) == 0) {
        return 0;
    }
    u64* pdpt = (u64*) (pml4[i4] & ~((u64) 0xFFF));
    if ((pdpt[i3] & 1) == 0) {
        return 0;
    }
    u64* pd = (u64*) (pdpt[i3] & ~((u64) 0xFFF));
    if ((pd[i2] & 1) == 0) {
        return 0;
    }
    u64* pt = (u64*) (pd[i2] & ~((u64) 0xFFF));
    if ((pt[i1] & 1) == 0) {
        return 0;
    }
    return (pt[i1] & ~(((u64) 0xFFF) | PAGE_NX)) | (vaddr & 0xFFF);
}

// New PML4 sharing the kernel's PDPT[0] (identity map) and PDPT[1] (heap) as
// the same physical sub-tables. PDPT[2]+ (vaddr >= 0x80000000) is left not
// present - private per-process region, populated lazily by map_page_in().
u64 clone_address_space(void) {
    void* new_pml4_frame = alloc_frame();
    if (new_pml4_frame == NULL) {
        return 0;
    }
    void* new_pdpt_frame = alloc_frame();
    if (new_pdpt_frame == NULL) {
        return 0;
    }
    zero_page(new_pml4_frame);
    zero_page(new_pdpt_frame);

    u64* kernel_pml4 = (u64*) g_pml4_phys;
    u64* kernel_pdpt = (u64*) (kernel_pml4[0] & ~((u64) 0xFFF));
    u64* new_pdpt = (u64*) new_pdpt_frame;
    // Strip the user bit on the shared kernel/heap entries - ring3 never needs
    // direct access there (syscalls raise CPL to 0 first). PML4[0] itself keeps
    // its user bit since it also covers this process's private region (PDPT[2]+),
    // which does need ring3 access; permission is the AND of every level.
    new_pdpt[0] = kernel_pdpt[0] & ~((u64) 0x04);  // shared: static identity map
    new_pdpt[1] = kernel_pdpt[1] & ~((u64) 0x04);  // shared: heap / dynamic-demo region

    u64* new_pml4 = (u64*) new_pml4_frame;
    new_pml4[0] = ((u64) new_pdpt_frame) | 0x07;  // present + writable + user

    return (u64) new_pml4_frame;
}

// Frees every private-region frame (PDPT[2]+: PT/PD/leaf data pages)
// plus the process's own PML4/PDPT frames. Never touches PDPT[0]/[1] -
// those are the shared kernel/heap sub-tables. Caller must not still be
// running on this address space (or its own kernel stack, which this
// doesn't touch).
void free_address_space(u64 pml4_phys) {
    u64* pml4 = (u64*) pml4_phys;
    u64* pdpt = (u64*) (pml4[0] & ~((u64) 0xFFF));
    u32 i3 = 2;
    while (i3 < 512) {
        if ((pdpt[i3] & 1) != 0) {
            u64* pd = (u64*) (pdpt[i3] & ~((u64) 0xFFF));
            u32 i2 = 0;
            while (i2 < 512) {
                if ((pd[i2] & 1) != 0) {
                    u64* pt = (u64*) (pd[i2] & ~((u64) 0xFFF));
                    u32 i1 = 0;
                    while (i1 < 512) {
                        if ((pt[i1] & 1) != 0) {
                            free_frame((void*) (pt[i1] & ~((u64) 0xFFF)));
                        }
                        i1 = i1 + 1;
                    }
                    free_frame((void*) (pd[i2] & ~((u64) 0xFFF)));
                }
                i2 = i2 + 1;
            }
            free_frame((void*) (pdpt[i3] & ~((u64) 0xFFF)));
        }
        i3 = i3 + 1;
    }
    free_frame((void*) (pml4[0] & ~((u64) 0xFFF)));
    free_frame((void*) pml4_phys);
}

void load_cr3(u64 phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

// TSS.RSP0 is an absolute reset point the CPU loads on every ring3->ring0
// transition - must be switched per-task, or a concurrent transition corrupts it.
#pragma GCC visibility push(hidden)
extern u8 tss_start;
#pragma GCC visibility pop

void set_tss_rsp0(u64 rsp0) {
    u32* low = (u32*) ((u64) &tss_start + 4);
    u32* high = (u32*) ((u64) &tss_start + 8);
    *low = (u32) (rsp0 & 0xFFFFFFFF);
    *high = (u32) (rsp0 >> 32);
}
