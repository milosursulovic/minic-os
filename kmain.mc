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

extern void isr0();
extern void isr13();
extern void isr14();
extern void irq0();
extern void irq1();

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

// ---- Heap: a straight bump allocator over a reserved 1MB arena, backed
// by .bss and already covered by the flat identity map from milestone 1.
// No `free` - same "arena" spirit as examples/allocator_demo.mc, just
// with `kreset` standing in for arenaReset().
u8 gHeapArena[1048576];
u64 gHeapUsed;

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
    while (s[i] != (char) 0) {
        serialPutc((u8) s[i]);
        i = i + 1;
    }
}

void vgaPutc(char c) {
    gVga[gVgaCursor].character = (u8) c;
    gVga[gVgaCursor].color = 0x0F;
    gVgaCursor = gVgaCursor + 1;
}

void vgaPrint(char* s) {
    int i = 0;
    while (s[i] != (char) 0) {
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

// No char literals in MiniC yet - these are plain ASCII codes ('a' = 97, etc).
void initScancodeTable() {
    gScancodeTable[0x1E] = (char) 97;  gScancodeTable[0x30] = (char) 98;  gScancodeTable[0x2E] = (char) 99;
    gScancodeTable[0x20] = (char) 100; gScancodeTable[0x12] = (char) 101; gScancodeTable[0x21] = (char) 102;
    gScancodeTable[0x22] = (char) 103; gScancodeTable[0x23] = (char) 104; gScancodeTable[0x17] = (char) 105;
    gScancodeTable[0x24] = (char) 106; gScancodeTable[0x25] = (char) 107; gScancodeTable[0x26] = (char) 108;
    gScancodeTable[0x32] = (char) 109; gScancodeTable[0x31] = (char) 110; gScancodeTable[0x18] = (char) 111;
    gScancodeTable[0x19] = (char) 112; gScancodeTable[0x10] = (char) 113; gScancodeTable[0x13] = (char) 114;
    gScancodeTable[0x1F] = (char) 115; gScancodeTable[0x14] = (char) 116; gScancodeTable[0x16] = (char) 117;
    gScancodeTable[0x2F] = (char) 118; gScancodeTable[0x11] = (char) 119; gScancodeTable[0x2D] = (char) 120;
    gScancodeTable[0x15] = (char) 121; gScancodeTable[0x2C] = (char) 122;
    gScancodeTable[0x39] = (char) 32;   // space
    gScancodeTable[0x1C] = (char) 10;   // enter -> newline
}

// ---- Heap ---------------------------------------------------------------

void* kalloc(u64 size) {
    u64 aligned = gHeapUsed;
    if (aligned % 16 != 0) {
        aligned = aligned + (16 - (aligned % 16));
    }
    if (aligned + size > 1048576) {
        return null;
    }
    u8* base = gHeapArena;
    void* ptr = (void*) (base + aligned);
    gHeapUsed = aligned + size;
    return ptr;
}

void kreset() {
    gHeapUsed = 0;
}

// ---- Small string/number helpers - no libc, so these are hand-rolled. --

bool streq(char* a, char* b) {
    int i = 0;
    while (a[i] != (char) 0 && b[i] != (char) 0) {
        if (a[i] != b[i]) {
            return false;
        }
        i = i + 1;
    }
    return a[i] == b[i];
}

void printHex(u64 value) {
    char* digits = "0123456789abcdef";
    char buf[17];
    buf[16] = (char) 0;
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
    vgaPrint("commands: help clear ticks alloc reset");
    serialPrint("commands: help clear ticks alloc reset\n");
}

void cmdClear() {
    int i = 80;   // leave the boot message on row 0
    while (i < 2000) {
        gVga[i].character = (u8) 32;
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
        vgaPrint("allocated 64 bytes at 0x");
        printHex((u64) p);
    }
}

void cmdReset() {
    kreset();
    vgaPrint("heap reset");
    serialPrint("heap reset\n");
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
    } else if (streq(gLineBuffer, "reset")) {
        cmdReset();
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
            serialPutc(46);    // '.' - one dot per ~1 second at 100Hz, proves the timer keeps firing
        }
        outb(0x20, 0x20);      // EOI
        return;
    }

    if (vector == 33) {
        u8 scancode = inb(0x60);
        if (scancode < 0x80) {   // top bit set = key release, ignore those
            char c = gScancodeTable[scancode];
            if (c == (char) 10) {
                gLineBuffer[gLineLen] = (char) 0;
                gLineReady = true;
            } else if (c != (char) 0 && gLineLen < 127) {
                gLineBuffer[gLineLen] = c;
                gLineLen = gLineLen + 1;
                vgaPutc(c);
                serialPutc((u8) c);
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
    while (message[i] != (char) 0) {
        vga[i].character = (u8) message[i];
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
