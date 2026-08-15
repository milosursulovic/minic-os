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
        table[index] = ((u64) newTable) | 0x03;   // present + writable
        return (u64*) newTable;
    }
    return (u64*) (entry & ~((u64) 0xFFF));
}

// Maps one 4KB page: vaddr -> paddr, creating any missing PDPT/PD/PT
// tables along the way. `flags` is OR'd into the final PT entry on top
// of the present bit (e.g. 0x02 for writable).
bool mapPage(u64 vaddr, u64 paddr, u64 flags) {
    u32 i4 = (u32) ((vaddr >> 39) & 0x1FF);
    u32 i3 = (u32) ((vaddr >> 30) & 0x1FF);
    u32 i2 = (u32) ((vaddr >> 21) & 0x1FF);
    u32 i1 = (u32) ((vaddr >> 12) & 0x1FF);

    u64* pml4 = (u64*) gPML4Phys;
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
    invalidatePage(vaddr);
    return true;
}
