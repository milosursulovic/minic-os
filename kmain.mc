// The MiniC side of the kernel. By the time boot.s's `call _start` reaches
// here, we're in 64-bit long mode with a real stack and the first 1GB of
// physical memory identity-mapped - including the VGA text buffer at
// 0xB8000, which needs no driver at all: it's just memory.
//
// Milestone 2 adds interrupts: an IDT built as an ordinary MiniC array of
// `packed struct` entries, the 8259 PIC remapped off the CPU's own
// exception vectors, the PIT reconfigured to a friendlier ~100Hz, and a
// real timer + keyboard handler. The entry stubs interrupts.s jumps to
// are hand-written (see that file for why); everything past "here's the
// vector number and error code" is ordinary MiniC.
//
// Milestone 3 adds a heap (a bump allocator over a reserved chunk of the
// milestone-1 identity map - real dynamic page-table management is a
// later problem, not needed yet with 1GB already flat-mapped) and a
// minimal interactive shell built on the keyboard/VGA plumbing above.
// Milestone 4 upgrades the heap to a real free-list allocator (kfree,
// splitting, coalescing).
//
// Milestone 5 adds real physical memory awareness: the multiboot memory
// map (parsed from the info structure the bootloader hands us in EBX -
// preserved by boot.s into gMultibootInfoPtr, since `_start` takes no
// parameters) drives a frame bitmap allocator. Still entirely within the
// milestone-1 flat 1GB map - using that memory for anything beyond the
// heap arena (i.e. dynamically extending the page tables themselves) is
// its own later problem.

struct VgaChar {
    u8 character;
    u8 color;
}

packed struct IdtEntry {
    u16 offsetLow;
    u16 selector;
    u8 ist;
    u8 typeAttr;
    u16 offsetMid;
    u32 offsetHigh;
    u32 zero;
}

packed struct IdtPointer {
    u16 limit;
    u64 base;
}

packed struct MultibootInfo {
    u32 flags;
    u32 memLower;
    u32 memUpper;
    u32 bootDevice;
    u32 cmdline;
    u32 modsCount;
    u32 modsAddr;
    u32 syms0;
    u32 syms1;
    u32 syms2;
    u32 syms3;
    u32 mmapLength;
    u32 mmapAddr;
}

// `size` is the byte count of the rest of THIS entry, not counting itself
// - entries aren't necessarily a fixed stride, so `addr` genuinely does
// sit at an unaligned 4-byte offset by the real spec. Without `packed`,
// MiniC would insert 4 bytes of padding before `addr` to 8-byte-align it
// and silently read the wrong bytes - exactly the case `packed` exists for.
packed struct MmapEntry {
    u32 size;
    u64 addr;
    u64 len;
    u32 type;   // 1 = available RAM
}

extern void isr0();
extern void isr13();
extern void isr14();
extern void irq0();
extern void irq1();

u32 gMultibootInfoPtr;

IdtEntry gIdt[256];
IdtPointer gIdtPtr;

volatile VgaChar* gVga;
int gVgaCursor;

u16 gOutPort;
u8 gOutByte;
u16 gInPort;
u8 gInByte;

u64 gTickCount;
char gScancodeTable[128];

// ---- Shell line buffer, filled a character at a time by the keyboard
// handler; the main loop (not the interrupt handler - keep that minimal)
// processes a line once Enter sets gLineReady.
char gLineBuffer[128];
int gLineLen;
bool gLineReady;

// ---- Heap: a real free-list allocator (split on alloc, forward-coalesce
// on free) over a reserved 1MB arena, backed by .bss and already covered
// by the flat identity map from milestone 1. Blocks stay in address
// order, so "the next block" is always `offset + sizeof(header) +
// size` - no separate `next` pointer needed, and coalescing two adjacent
// free blocks is just folding one header's size into its neighbor's.
// Backward coalescing (merging into the *previous* block) isn't done -
// would need a full rescan from the start to find it; a real placement-
// sensitive allocator profile would want it, this one doesn't need it yet.
struct BlockHeader {
    u64 size;    // usable bytes *after* this header, not counting the header itself
    bool free;
}

u8 gHeapArena[1048576];
bool gHeapInited;
void* gLastAlloc;

// ---- Raw port I/O - the one thing asm(...) has to do directly, since
// there's no operand binding to hand it a MiniC value. Everything else
// (PIC/PIT setup, the IDT, the handlers themselves) is ordinary MiniC
// built on top of these two.
void outb(u16 port, u8 value) {
    gOutPort = port;
    gOutByte = value;
    asm("mov dx, [rip+gOutPort]\nmov al, [rip+gOutByte]\nout dx, al");
}

u8 inb(u16 port) {
    gInPort = port;
    asm("mov dx, [rip+gInPort]\nin al, dx\nmov [rip+gInByte], al");
    return gInByte;
}

void serialPutc(u8 c) {
    outb(0x3F8, c);
}

void serialPrint(char* s) {
    int i = 0;
    while (s[i] != '\0') {
        serialPutc(s[i]);
        i = i + 1;
    }
}

void vgaPutc(char c) {
    gVga[gVgaCursor].character = c;
    gVga[gVgaCursor].color = 0x0F;
    gVgaCursor = gVgaCursor + 1;
}

void vgaPrint(char* s) {
    int i = 0;
    while (s[i] != '\0') {
        vgaPutc(s[i]);
        i = i + 1;
    }
}

// ---- IDT --------------------------------------------------------------

void setIdtEntry(int vector, u64 handlerAddr) {
    gIdt[vector].offsetLow = (u16) handlerAddr;
    gIdt[vector].selector = 0x08;    // the code64 selector from boot.s's GDT
    gIdt[vector].ist = 0;
    gIdt[vector].typeAttr = 0x8E;    // present, ring0, 64-bit interrupt gate
    gIdt[vector].offsetMid = (u16) (handlerAddr >> 16);
    gIdt[vector].offsetHigh = (u32) (handlerAddr >> 32);
    gIdt[vector].zero = 0;
}

void idtInit() {
    setIdtEntry(0, (u64) &isr0);
    setIdtEntry(13, (u64) &isr13);
    setIdtEntry(14, (u64) &isr14);
    setIdtEntry(32, (u64) &irq0);
    setIdtEntry(33, (u64) &irq1);

    IdtEntry* base = gIdt;
    gIdtPtr.limit = (u16) (sizeof(IdtEntry) * 256 - 1);
    gIdtPtr.base = (u64) base;
    // Unlike boot.s's `lgdt` (which runs before the jump to long mode, so
    // it reads the 32-bit GDTR format), this `lidt` runs after we're
    // already in 64-bit mode, so gIdtPtr's full 10-byte layout applies.
    asm("lidt [rip+gIdtPtr]");
}

// ---- 8259 PIC: remapped off the CPU's own exception vectors (0-31,
// where IRQ0-7 collide by default) onto 32-47, with only the timer/
// keyboard lines (IRQ0/IRQ1) unmasked - nothing else is handled yet.
void picRemap() {
    outb(0x20, 0x11);
    outb(0xA0, 0x11);
    outb(0x21, 0x20);   // master PIC vector offset
    outb(0xA1, 0x28);   // slave PIC vector offset
    outb(0x21, 0x04);
    outb(0xA1, 0x02);
    outb(0x21, 0x01);
    outb(0xA1, 0x01);
    outb(0x21, 0xFC);   // unmask IRQ0 (timer) and IRQ1 (keyboard) only
    outb(0xA1, 0xFF);   // mask everything on the slave PIC - unhandled for now
}

// ~100Hz (1193182Hz base / 100). Default is ~18.2Hz - too slow to prove
// the timer's actually ticking within a short test run.
void pitInit() {
    u16 divisor = 11932;
    outb(0x43, 0x36);
    outb(0x40, (u8) divisor);
    outb(0x40, (u8) (divisor >> 8));
}

void initScancodeTable() {
    gScancodeTable[0x1E] = 'a'; gScancodeTable[0x30] = 'b'; gScancodeTable[0x2E] = 'c';
    gScancodeTable[0x20] = 'd'; gScancodeTable[0x12] = 'e'; gScancodeTable[0x21] = 'f';
    gScancodeTable[0x22] = 'g'; gScancodeTable[0x23] = 'h'; gScancodeTable[0x17] = 'i';
    gScancodeTable[0x24] = 'j'; gScancodeTable[0x25] = 'k'; gScancodeTable[0x26] = 'l';
    gScancodeTable[0x32] = 'm'; gScancodeTable[0x31] = 'n'; gScancodeTable[0x18] = 'o';
    gScancodeTable[0x19] = 'p'; gScancodeTable[0x10] = 'q'; gScancodeTable[0x13] = 'r';
    gScancodeTable[0x1F] = 's'; gScancodeTable[0x14] = 't'; gScancodeTable[0x16] = 'u';
    gScancodeTable[0x2F] = 'v'; gScancodeTable[0x11] = 'w'; gScancodeTable[0x2D] = 'x';
    gScancodeTable[0x15] = 'y'; gScancodeTable[0x2C] = 'z';
    gScancodeTable[0x39] = ' ';
    gScancodeTable[0x1C] = '\n';   // enter -> newline

    // digit row, for typing hex addresses back into `free <addr>`
    gScancodeTable[0x02] = '1'; gScancodeTable[0x03] = '2'; gScancodeTable[0x04] = '3';
    gScancodeTable[0x05] = '4'; gScancodeTable[0x06] = '5'; gScancodeTable[0x07] = '6';
    gScancodeTable[0x08] = '7'; gScancodeTable[0x09] = '8'; gScancodeTable[0x0A] = '9';
    gScancodeTable[0x0B] = '0';
}

// ---- Heap ---------------------------------------------------------------

BlockHeader* blockAt(u64 offset) {
    u8* base = gHeapArena;
    return (BlockHeader*) (base + offset);
}

void heapInit() {
    u64 headerSize = sizeof(BlockHeader);
    BlockHeader* first = blockAt(0);
    first->size = 1048576 - headerSize;
    first->free = true;
    gHeapInited = true;
}

void* kalloc(u64 size) {
    if (!gHeapInited) {
        heapInit();
    }
    if (size % 16 != 0) {
        size = size + (16 - (size % 16));
    }
    u64 headerSize = sizeof(BlockHeader);

    u64 offset = 0;
    while (offset < 1048576) {
        BlockHeader* block = blockAt(offset);
        if (block->free && block->size >= size) {
            // Split off the remainder as a new free block, but only if
            // there's enough room left for another header plus something
            // worth having - otherwise just hand over the whole block.
            if (block->size >= size + headerSize + 16) {
                u64 remainderOffset = offset + headerSize + size;
                BlockHeader* remainder = blockAt(remainderOffset);
                remainder->size = block->size - size - headerSize;
                remainder->free = true;
                block->size = size;
            }
            block->free = false;
            u8* blockBytes = (u8*) block;
            return (void*) (blockBytes + headerSize);
        }
        offset = offset + headerSize + block->size;
    }
    return null;
}

void kfree(void* ptr) {
    if (ptr == null) {
        return;
    }
    u64 headerSize = sizeof(BlockHeader);
    u8* base = gHeapArena;
    u64 baseAddr = (u64) base;
    u64 ptrAddr = (u64) ptr;
    // A bogus pointer (e.g. a stale/mistyped address from `free <addr>`)
    // would otherwise underflow this subtraction to a huge offset and
    // either corrupt unrelated memory or fault - found this the hard way
    // testing `free <addr>` with an address from a previous build. Ignore
    // it instead of trusting it.
    if (ptrAddr < baseAddr + headerSize || ptrAddr >= baseAddr + 1048576) {
        return;
    }
    u64 offset = ptrAddr - baseAddr - headerSize;

    BlockHeader* block = blockAt(offset);
    block->free = true;

    // Forward-coalesce: fold in every immediately-following block while
    // it's also free, since blocks are laid out contiguously in address
    // order - no pointer-chasing needed to find "the next one".
    u64 nextOffset = offset + headerSize + block->size;
    while (nextOffset < 1048576) {
        BlockHeader* nextBlock = blockAt(nextOffset);
        if (!nextBlock->free) {
            break;
        }
        block->size = block->size + headerSize + nextBlock->size;
        nextOffset = offset + headerSize + block->size;
    }

    // Backward-coalesce: blocks have no back-pointer, so finding the one
    // immediately *before* this one means rescanning from the arena
    // start - O(n) per free, fine for a hobby heap, not something a real
    // allocator would want.
    u64 scanOffset = 0;
    u64 prevOffset = offset;
    bool foundPrev = false;
    while (scanOffset < offset) {
        BlockHeader* scanBlock = blockAt(scanOffset);
        if (scanOffset + headerSize + scanBlock->size == offset) {
            prevOffset = scanOffset;
            foundPrev = true;
            break;
        }
        scanOffset = scanOffset + headerSize + scanBlock->size;
    }
    if (foundPrev) {
        BlockHeader* prevBlock = blockAt(prevOffset);
        if (prevBlock->free) {
            prevBlock->size = prevBlock->size + headerSize + block->size;
        }
    }
}

u64 heapFreeBytes() {
    if (!gHeapInited) {
        heapInit();
    }
    u64 headerSize = sizeof(BlockHeader);
    u64 total = 0;
    u64 offset = 0;
    while (offset < 1048576) {
        BlockHeader* block = blockAt(offset);
        if (block->free) {
            total = total + block->size;
        }
        offset = offset + headerSize + block->size;
    }
    return total;
}

// ---- Physical frame allocator - 1 bit per 4KB frame, covering the whole
// milestone-1 identity-mapped 1GB (32768 bytes * 8 bits * 4KB = 1GB).
// Distinct from the heap above: the heap hands out *virtual* bytes within
// a fixed 1MB arena for kernel data structures; this tracks *physical*
// frames or a real page-table layout to actually manage. Real dynamic
// paging (mapping memory beyond the flat 1GB, or handing frames to a
// process) is the next problem this unblocks, not solved yet.
u8 gFrameBitmap[32768];
u32 gTotalFrames;
u32 gFreeFrameCount;

void frameSet(u32 frame) {
    u32 byteIndex = frame / 8;
    u8 mask = (u8) (1 << (frame % 8));
    gFrameBitmap[byteIndex] = gFrameBitmap[byteIndex] | mask;
}

bool frameTest(u32 frame) {
    u32 byteIndex = frame / 8;
    u8 mask = (u8) (1 << (frame % 8));
    return (gFrameBitmap[byteIndex] & mask) != 0;
}

void frameClear(u32 frame) {
    u32 byteIndex = frame / 8;
    u8 mask = (u8) (1 << (frame % 8));
    gFrameBitmap[byteIndex] = gFrameBitmap[byteIndex] & (~mask);
}

// Everything starts "used"; the multiboot memory map (type 1 = available
// RAM) clears the frames that are actually free to hand out. The first
// 4MB is reserved unconditionally regardless of what the map says -
// simpler than computing exactly where the kernel image/heap arena/this
// very bitmap end, and there's plenty of room to spare.
void framesInit() {
    u32 i = 0;
    while (i < 32768) {
        gFrameBitmap[i] = 255;
        i = i + 1;
    }
    gTotalFrames = 262144;   // 1GB / 4KB
    gFreeFrameCount = 0;

    MultibootInfo* info = (MultibootInfo*) ((u64) gMultibootInfoPtr);
    u64 mmapAddr = (u64) info->mmapAddr;
    u64 mmapEnd = mmapAddr + (u64) info->mmapLength;
    u64 entryAddr = mmapAddr;
    while (entryAddr < mmapEnd) {
        MmapEntry* entry = (MmapEntry*) entryAddr;
        if (entry->type == 1) {
            u64 start = entry->addr;
            u64 end = entry->addr + entry->len;
            if (start < 4194304) {
                start = 4194304;
            }
            if (end > 1073741824) {
                end = 1073741824;
            }
            u64 frame = start / 4096;
            u64 frameEnd = end / 4096;
            while (frame < frameEnd) {
                if (frameTest((u32) frame)) {
                    frameClear((u32) frame);
                    gFreeFrameCount = gFreeFrameCount + 1;
                }
                frame = frame + 1;
            }
        }
        entryAddr = entryAddr + (u64) entry->size + 4;
    }
}

void* allocFrame() {
    u32 i = 0;
    while (i < gTotalFrames) {
        if (!frameTest(i)) {
            frameSet(i);
            gFreeFrameCount = gFreeFrameCount - 1;
            u64 addr = (u64) i * 4096;
            return (void*) addr;
        }
        i = i + 1;
    }
    return null;
}

void freeFrame(void* addr) {
    u64 a = (u64) addr;
    u32 frame = (u32) (a / 4096);
    if (frameTest(frame)) {
        frameClear(frame);
        gFreeFrameCount = gFreeFrameCount + 1;
    }
}

// ---- Small string/number helpers - no libc, so these are hand-rolled. --

bool streq(char* a, char* b) {
    int i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) {
            return false;
        }
        i = i + 1;
    }
    return a[i] == b[i];
}

bool startsWith(char* s, char* prefix) {
    int i = 0;
    while (prefix[i] != '\0') {
        if (s[i] != prefix[i]) {
            return false;
        }
        i = i + 1;
    }
    return true;
}

// Accepts an optional "0x" prefix; any non-hex-digit character is simply
// skipped rather than treated as an error - good enough for a shell
// that's only ever fed its own printHex() output back.
u64 parseHex(char* s) {
    u64 value = 0;
    int i = 0;
    if (s[0] == '0' && s[1] == 'x') {
        i = 2;
    }
    while (s[i] != '\0') {
        char c = s[i];
        u64 digit = 0;
        bool validDigit = true;
        if (c >= '0' && c <= '9') {
            digit = (u64) (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (u64) (c - 'a') + 10;
        } else {
            validDigit = false;
        }
        if (validDigit) {
            value = value * 16 + digit;
        }
        i = i + 1;
    }
    return value;
}

void printHex(u64 value) {
    char* digits = "0123456789abcdef";
    char buf[17];
    buf[16] = '\0';
    if (value == 0) {
        buf[15] = digits[0];
        vgaPrint(&buf[15]);
        serialPrint(&buf[15]);
        return;
    }
    int i = 15;
    while (value > 0 && i >= 0) {
        u64 nibble = value % 16;
        buf[i] = digits[nibble];
        value = value / 16;
        i = i - 1;
    }
    vgaPrint(&buf[i + 1]);
    serialPrint(&buf[i + 1]);
}

// ---- Shell ----------------------------------------------------------------

void printPrompt() {
    vgaPrint("> ");
    serialPrint("> ");
}

void cmdHelp() {
    vgaPrint("commands: help clear ticks alloc free free <addr> mem reset frame frames echo <text>");
    serialPrint("commands: help clear ticks alloc free free <addr> mem reset frame frames echo <text>\n");
}

void cmdClear() {
    int i = 80;   // leave the boot message on row 0
    while (i < 2000) {
        gVga[i].character = ' ';
        gVga[i].color = 0x07;
        i = i + 1;
    }
    gVgaCursor = 80;
}

void cmdTicks() {
    vgaPrint("ticks: 0x");
    printHex(gTickCount);
}

void cmdAlloc() {
    void* p = kalloc(64);
    if (p == null) {
        vgaPrint("alloc failed - heap full");
        serialPrint("alloc failed - heap full\n");
    } else {
        gLastAlloc = p;
        vgaPrint("allocated 64 bytes at 0x");
        serialPrint("allocated 64 bytes at 0x");
        printHex((u64) p);
    }
}

void cmdFree() {
    if (gLastAlloc == null) {
        vgaPrint("nothing to free");
        serialPrint("nothing to free\n");
        return;
    }
    vgaPrint("freed 0x");
    serialPrint("freed 0x");
    printHex((u64) gLastAlloc);
    kfree(gLastAlloc);
    gLastAlloc = null;
}

void cmdMem() {
    vgaPrint("free: 0x");
    serialPrint("free: 0x");
    printHex(heapFreeBytes());
}

void cmdReset() {
    heapInit();
    gLastAlloc = null;
    vgaPrint("heap reset");
    serialPrint("heap reset\n");
}

void cmdEcho() {
    char* text = &gLineBuffer[5];   // past "echo "
    vgaPrint(text);
    serialPrint(text);
}

void cmdFreeAddr() {
    u64 addr = parseHex(&gLineBuffer[5]);   // past "free "
    kfree((void*) addr);
    vgaPrint("freed 0x");
    serialPrint("freed 0x");
    printHex(addr);
}

void cmdFrames() {
    vgaPrint("free frames: 0x");
    serialPrint("free frames: 0x");
    printHex((u64) gFreeFrameCount);
    vgaPutc(' ');
    serialPutc(' ');
    vgaPrint("/ 0x");
    serialPrint("/ 0x");
    printHex((u64) gTotalFrames);
}

void cmdFrame() {
    void* f = allocFrame();
    if (f == null) {
        vgaPrint("out of frames");
        serialPrint("out of frames\n");
    } else {
        vgaPrint("allocated frame at 0x");
        serialPrint("allocated frame at 0x");
        printHex((u64) f);
    }
}

void runCommand() {
    if (streq(gLineBuffer, "help")) {
        cmdHelp();
    } else if (streq(gLineBuffer, "clear")) {
        cmdClear();
    } else if (streq(gLineBuffer, "ticks")) {
        cmdTicks();
    } else if (streq(gLineBuffer, "alloc")) {
        cmdAlloc();
    } else if (streq(gLineBuffer, "free")) {
        cmdFree();
    } else if (startsWith(gLineBuffer, "free ")) {
        cmdFreeAddr();
    } else if (streq(gLineBuffer, "mem")) {
        cmdMem();
    } else if (streq(gLineBuffer, "reset")) {
        cmdReset();
    } else if (streq(gLineBuffer, "frames")) {
        cmdFrames();
    } else if (streq(gLineBuffer, "frame")) {
        cmdFrame();
    } else if (startsWith(gLineBuffer, "echo ")) {
        cmdEcho();
    } else if (gLineLen > 0) {
        vgaPrint("unknown command");
        serialPrint("unknown command\n");
    }
}

// Called from interrupts.s's isr_common_stub for every vector it knows
// about. Real MiniC, called with an ordinary `call` - a normal function,
// nothing interrupt-specific about its body.
void interrupt_handler(u64 vector, u64 errorCode) {
    if (vector == 32) {
        gTickCount = gTickCount + 1;
        if (gTickCount % 100 == 0) {
            serialPutc('.');   // one dot per ~1 second at 100Hz, proves the timer keeps firing
        }
        outb(0x20, 0x20);      // EOI
        return;
    }

    if (vector == 33) {
        u8 scancode = inb(0x60);
        if (scancode < 0x80) {   // top bit set = key release, ignore those
            char c = gScancodeTable[scancode];
            if (c == '\n') {
                gLineBuffer[gLineLen] = '\0';
                gLineReady = true;
            } else if (c != '\0' && gLineLen < 127) {
                gLineBuffer[gLineLen] = c;
                gLineLen = gLineLen + 1;
                vgaPutc(c);
                serialPutc(c);
            }
        }
        outb(0x20, 0x20);
        return;
    }

    if (vector == 0) {
        serialPrint("divide by zero, halting\n");
    } else if (vector == 13) {
        serialPrint("general protection fault, halting\n");
    } else if (vector == 14) {
        serialPrint("page fault, halting\n");
    } else {
        serialPrint("unhandled exception, halting\n");
    }
    while (true) {
        asm("hlt");
    }
}

void _start() {
    volatile VgaChar* vga = (volatile VgaChar*) 0xB8000;
    char* message = "Hello from a MiniC kernel!";
    int i = 0;
    while (message[i] != '\0') {
        vga[i].character = message[i];
        vga[i].color = 0x0F;
        i = i + 1;
    }
    serialPrint("Hello from a MiniC kernel!\n");

    gVga = (volatile VgaChar*) 0xB8000;
    gVgaCursor = 80;   // second row - leave the boot message on row 0
    initScancodeTable();

    idtInit();
    picRemap();
    pitInit();
    framesInit();
    asm("sti");

    serialPrint("interrupts live\n");
    printPrompt();

    while (true) {
        asm("hlt");   // the CPU sleeps here between interrupts; every timer
                       // tick and keypress wakes it right back to this check
        if (gLineReady) {
            runCommand();
            gLineReady = false;
            gLineLen = 0;
            gVgaCursor = ((gVgaCursor / 80) + 1) * 80;   // next row
            if (gVgaCursor >= 2000) {
                gVgaCursor = 80;   // wrap - no real scrolling yet
            }
            printPrompt();
        }
    }
}
