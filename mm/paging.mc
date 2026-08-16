// Real PML4/PDPT/PD/PT walking and creation, using frames.mc's allocator
// to supply new table pages. boot.s's static map only covers the first
// 1GB with 2MB huge pages and stops at the PD level (no PT); this is
// what lets the kernel map memory ANYWHERE the frame allocator can back
// it, not just inside that fixed range.
//
// Every physical address this code touches - the tables themselves, and
// every frame allocFrame() hands out - is guaranteed inside the first
// 1GB, which boot.s already identity-maps. That's what makes this doable
// in ordinary MiniC with plain pointer dereferences: no temporary
// mapping trick or recursive self-map is needed just to edit a table.
//
// Known limitation: mapPage() must not be called on a virtual address
// that falls inside boot.s's static 1GB identity map (PDPT index 0) -
// the PD entries there are 2MB huge pages (PS bit set), and walking past
// one as if it pointed to a PT would read a garbage table address.

import "frames.mc";

u64 gPML4Phys;

void readPML4() {
    asm("mov rax, cr3\nmov [rip+gPML4Phys], rax");
}

u64 gInvlpgAddr;

void invalidatePage(u64 vaddr) {
    gInvlpgAddr = vaddr;
    asm("mov rax, [rip+gInvlpgAddr]\ninvlpg [rax]");
}

u64 gCr2Value;

// CR2 holds the faulting virtual address for a page fault (vector 14) -
// the CPU sets it right before delivering the exception. Only valid to
// read from inside that handler.
void readCr2() {
    asm("mov rax, cr2\nmov [rip+gCr2Value], rax");
}

void zeroPage(void* p) {
    u64* words = (u64*) p;
    u32 i = 0;
    while (i < 512) {
        words[i] = 0;
        i = i + 1;
    }
}

// Returns the next-level table's physical address, allocating and
// zeroing a fresh frame for it if this entry isn't present yet.
u64* tableGetOrCreate(u64* table, u32 index) {
    u64 entry = table[index];
    if ((entry & 1) == 0) {
        void* newTable = allocFrame();
        if (newTable == null) {
            return null;
        }
        zeroPage(newTable);
        // Milestone 11: user (0x04) added alongside present+writable. This
        // doesn't loosen any existing protection - the CPU ANDs the user
        // bit across every level from PML4 down to the leaf PT entry, so a
        // supervisor-only *leaf* (mapPage() callers not passing 0x04, which
        // is everything except the ring3 test's own stack today) still
        // blocks user access regardless of what intermediate tables allow.
        // Without this, an intermediate table created for one purpose
        // (e.g. the heap) could accidentally block user access to a
        // completely different, unrelated leaf mapped later through the
        // same table - this just removes that possibility.
        table[index] = ((u64) newTable) | 0x07;   // present + writable + user
        return (u64*) newTable;
    }
    return (u64*) (entry & ~((u64) 0xFFF));
}

// Maps one 4KB page: vaddr -> paddr in an EXPLICITLY given address
// space, creating any missing PDPT/PD/PT tables along the way. `flags`
// is OR'd into the final PT entry on top of the present bit (e.g. 0x02
// for writable). Every page-table frame allocFrame() hands out lives
// inside the flat 1GB identity map every address space shares, so this
// is a plain pointer walk regardless of which CR3 is currently loaded -
// kernel code can build up a process's private mappings without ever
// switching into it first.
bool mapPageIn(u64 pml4Phys, u64 vaddr, u64 paddr, u64 flags) {
    u32 i4 = (u32) ((vaddr >> 39) & 0x1FF);
    u32 i3 = (u32) ((vaddr >> 30) & 0x1FF);
    u32 i2 = (u32) ((vaddr >> 21) & 0x1FF);
    u32 i1 = (u32) ((vaddr >> 12) & 0x1FF);

    u64* pml4 = (u64*) pml4Phys;
    u64* pdpt = tableGetOrCreate(pml4, i4);
    if (pdpt == null) {
        return false;
    }
    u64* pd = tableGetOrCreate(pdpt, i3);
    if (pd == null) {
        return false;
    }
    u64* pt = tableGetOrCreate(pd, i2);
    if (pt == null) {
        return false;
    }

    pt[i1] = (paddr & ~((u64) 0xFFF)) | (flags & 0xFFF) | 0x01;
    if (pml4Phys == gPML4Phys) {
        invalidatePage(vaddr);   // only meaningful for the currently-loaded space
    }
    return true;
}

// Maps into the currently-loaded (kernel) address space - every caller
// before milestone 12 (the heap, the `map` demo, the ring3 test stack)
// keeps working unchanged.
bool mapPage(u64 vaddr, u64 paddr, u64 flags) {
    return mapPageIn(gPML4Phys, vaddr, paddr, flags);
}

// Read-only walk of an explicit address space, returning the physical
// address `vaddr` resolves to there (or 0 if any level isn't present -
// physical address 0 is never a real answer, the frame allocator's
// first allocatable frame starts well above the low-memory reservation).
// Used to prove two processes' identical virtual addresses land on
// genuinely different physical frames, not just that they read back
// different values (which a cache or a coincidence could also explain).
u64 translateIn(u64 pml4Phys, u64 vaddr) {
    u32 i4 = (u32) ((vaddr >> 39) & 0x1FF);
    u32 i3 = (u32) ((vaddr >> 30) & 0x1FF);
    u32 i2 = (u32) ((vaddr >> 21) & 0x1FF);
    u32 i1 = (u32) ((vaddr >> 12) & 0x1FF);

    u64* pml4 = (u64*) pml4Phys;
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
    return (pt[i1] & ~((u64) 0xFFF)) | (vaddr & 0xFFF);
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
// calls mapPageIn() there. This is what makes two processes mapping the
// same private-region virtual address land on different physical frames:
// they get separate PD/PT chains for that region, created independently.
u64 cloneAddressSpace() {
    void* newPml4Frame = allocFrame();
    if (newPml4Frame == null) {
        return 0;
    }
    void* newPdptFrame = allocFrame();
    if (newPdptFrame == null) {
        return 0;
    }
    zeroPage(newPml4Frame);
    zeroPage(newPdptFrame);

    u64* kernelPml4 = (u64*) gPML4Phys;
    u64* kernelPdpt = (u64*) (kernelPml4[0] & ~((u64) 0xFFF));
    u64* newPdpt = (u64*) newPdptFrame;
    newPdpt[0] = kernelPdpt[0];   // shared: static identity map
    newPdpt[1] = kernelPdpt[1];   // shared: heap / dynamic-demo region

    u64* newPml4 = (u64*) newPml4Frame;
    newPml4[0] = ((u64) newPdptFrame) | 0x07;   // present + writable + user

    return (u64) newPml4Frame;
}

u64 gCr3ToLoad;

void loadCr3(u64 phys) {
    gCr3ToLoad = phys;
    asm("mov rax, [rip+gCr3ToLoad]\nmov cr3, rax");
}

// Milestone 19: TSS.RSP0 switched per-task, the fix milestones 13/15
// both flagged as a prerequisite for ever running more than one ring3-
// capable task concurrently. A single shared RSP0 (boot.s's
// int_stack_top) was safe exactly as long as at most one ring3->ring0
// transition could ever be "in flight" (suspended, not yet resumed) at
// once - true when only one ring3 task existed at all, false the moment
// milestone 19's spawn command could create a second one alongside the
// one already running from boot. A real GPF (errorCode 0x32) confirmed
// this the first time two ring3 processes coexisted: the second one's
// own syscalls corrupted the first one's still-pending suspended state
// on the shared stack, the same bug *class* as milestone 11's original
// RSP0 discovery and milestone 13's demo-vs-real-mechanism conflict,
// now between two equally-real processes.
//
// No new instruction needed - RSP0 is an ordinary memory location the
// CPU reads automatically on a privilege-raising interrupt, not
// something a privileged instruction has to load (unlike CR3/GDTR).
// Plain pointer writes into the TSS's own bytes (tss_start+4/+8, the
// same offsets boot.s's own one-time patch already used) are enough.
extern u8 tss_start;

void setTssRsp0(u64 rsp0) {
    u32* low = (u32*) ((u64) &tss_start + 4);
    u32* high = (u32*) ((u64) &tss_start + 8);
    *low = (u32) (rsp0 & 0xFFFFFFFF);
    *high = (u32) (rsp0 >> 32);
}
