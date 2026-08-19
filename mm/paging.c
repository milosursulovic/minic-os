// Real PML4/PDPT/PD/PT walking and creation, using frames.c's allocator
// to supply new table pages. boot.s's static map only covers the first
// 1GB with 2MB huge pages and stops at the PD level (no PT); this is
// what lets the kernel map memory ANYWHERE the frame allocator can back
// it, not just inside that fixed range.
//
// Every physical address this code touches - the tables themselves, and
// every frame alloc_frame() hands out - is guaranteed inside the first
// 1GB, which boot.s already identity-maps. That's what makes this doable
// with plain pointer dereferences: no temporary mapping trick or
// recursive self-map is needed just to edit a table.
//
// Known limitation: map_page() must not be called on a virtual address
// that falls inside boot.s's static 1GB identity map (PDPT index 0) -
// the PD entries there are 2MB huge pages (PS bit set), and walking past
// one as if it pointed to a PT would read a garbage table address.

#include "paging.h"
#include "frames.h"

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

// Returns the next-level table's physical address, allocating and
// zeroing a fresh frame for it if this entry isn't present yet.
static u64* table_get_or_create(u64* table, u32 index) {
    u64 entry = table[index];
    if ((entry & 1) == 0) {
        void* new_table = alloc_frame();
        if (new_table == NULL) {
            return NULL;
        }
        zero_page(new_table);
        // user (0x04) added alongside present+writable. This doesn't
        // loosen any existing protection - the CPU ANDs the user bit
        // across every level from PML4 down to the leaf PT entry, so a
        // supervisor-only *leaf* (map_page() callers not passing 0x04)
        // still blocks user access regardless of what intermediate
        // tables allow.
        table[index] = ((u64) new_table) | 0x07;  // present + writable + user
        return (u64*) new_table;
    }
    return (u64*) (entry & ~((u64) 0xFFF));
}

// Maps one 4KB page: vaddr -> paddr in an EXPLICITLY given address
// space, creating any missing PDPT/PD/PT tables along the way. `flags`
// is OR'd into the final PT entry on top of the present bit (e.g. 0x02
// for writable, or PAGE_NX for non-executable). Every page-table frame
// alloc_frame() hands out lives inside the flat 1GB identity map every
// address space shares, so this is a plain pointer walk regardless of
// which CR3 is currently loaded - kernel code can build up a process's
// private mappings without ever switching into it first.
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

// Maps into the currently-loaded (kernel) address space - every caller
// that predates per-process address spaces (the heap, the `map` demo,
// the ring3 test stack) keeps working unchanged.
bool map_page(u64 vaddr, u64 paddr, u64 flags) {
    return map_page_in(g_pml4_phys, vaddr, paddr, flags);
}

// Read-only walk of an explicit address space, returning the physical
// address `vaddr` resolves to there (or 0 if any level isn't present -
// physical address 0 is never a real answer, the frame allocator's
// first allocatable frame starts well above the low-memory reservation).
// Used to prove two processes' identical virtual addresses land on
// genuinely different physical frames, not just that they read back
// different values (which a cache or a coincidence could also explain).
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
    // PAGE_NX (bit 63) can be set on a real leaf entry - must also clear
    // it here, same reasoning as map_page_in's own widened mask.
    return (pt[i1] & ~(((u64) 0xFFF) | PAGE_NX)) | (vaddr & 0xFFF);
}

// Builds a brand-new address space for a process: a fresh PML4 whose
// entry 0 points to a fresh PDPT that shares PDPT[0] (boot.s's static
// identity-mapped huge pages - kernel code/stack/GDT/IDT/TSS all live
// here) and PDPT[1] (the dynamic heap/demo region every task's stack
// already comes from, via kalloc) with the kernel's own PDPT - literally
// the same physical sub-tables, not copies, so a write through either
// address space is visible through the other there. Everything from
// PDPT[2] up (vaddr >= 0x80000000) is left NOT present - private to
// whichever process this becomes, populated on demand the first time it
// calls map_page_in() there. This is what makes two processes mapping the
// same private-region virtual address land on different physical frames:
// they get separate PD/PT chains for that region, created independently.
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
    // Strip the user (0x04) bit when copying the shared entries into a
    // NEW (potentially ring3-capable) address space. A ring3 process
    // never legitimately needs direct access to this region at all:
    // every interaction with kernel code/data/the heap happens through a
    // syscall (int 0x80), which raises CPL to 0 before any of that
    // memory is touched - x86 paging permission checks use CPL, not
    // "which segment the current instruction came from", so a syscall
    // handler keeps working identically here regardless of this bit.
    // Only the *copy* used by a cloned (ring3-capable) address space is
    // touched - the original kernel address space (g_pml4_phys, still
    // used directly by plain kernel-mode tasks) is untouched. PML4[0]
    // itself deliberately keeps its own user bit (see new_pml4[0]
    // below) - it also covers this process's PRIVATE region (PDPT[2],
    // vaddr >= 0x80000000), which the process DOES need ring3 access
    // to; x86 permission is the AND of every level, so stripping the
    // bit specifically at THIS PDPT entry is what blocks indices 0/1
    // while leaving index 2+ (populated later by map_page_in, WITH the
    // user bit) fully accessible.
    new_pdpt[0] = kernel_pdpt[0] & ~((u64) 0x04);  // shared: static identity map
    new_pdpt[1] = kernel_pdpt[1] & ~((u64) 0x04);  // shared: heap / dynamic-demo region

    u64* new_pml4 = (u64*) new_pml4_frame;
    new_pml4[0] = ((u64) new_pdpt_frame) | 0x07;  // present + writable + user

    return (u64) new_pml4_frame;
}

void load_cr3(u64 phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

// TSS.RSP0 switched per-task: a single shared RSP0 (boot.s's
// int_stack_top) is only safe as long as at most one ring3->ring0
// transition can ever be "in flight" (suspended, not yet resumed) at
// once - false the moment more than one ring3-capable task can exist
// concurrently. No new instruction needed - RSP0 is an ordinary memory
// location the CPU reads automatically on a privilege-raising interrupt,
// not something a privileged instruction has to load (unlike CR3/GDTR).
// Plain pointer writes into the TSS's own bytes (tss_start+4/+8, the
// same offsets boot.s's own one-time patch already used) are enough.
#pragma GCC visibility push(hidden)
extern u8 tss_start;
#pragma GCC visibility pop

void set_tss_rsp0(u64 rsp0) {
    u32* low = (u32*) ((u64) &tss_start + 4);
    u32* high = (u32*) ((u64) &tss_start + 8);
    *low = (u32) (rsp0 & 0xFFFFFFFF);
    *high = (u32) (rsp0 >> 32);
}
