// Everything needed to get interrupts flowing: the IDT (an ordinary
// MiniC array of `packed struct` entries), and the 8259 PIC/PIT config
// that makes the timer/keyboard lines usable. The entry stubs themselves
// (isr0/isr13/isr14/irq0/irq1) are hand-written in interrupts.s - saving/
// restoring full register state and normalizing "sometimes the CPU
// pushes an error code, sometimes it doesn't" is below what a MiniC
// function body can express - so they're declared `extern` here and
// just wired into the IDT by address.

import "io.mc";

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

// 8259 PIC: remapped off the CPU's own exception vectors (0-31, where
// IRQ0-7 collide by default) onto 32-47, with only the timer/keyboard
// lines (IRQ0/IRQ1) unmasked - nothing else is handled yet.
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
