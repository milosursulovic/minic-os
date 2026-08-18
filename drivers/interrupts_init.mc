// Everything needed to get interrupts flowing: the IDT (an ordinary
// MiniC array of `packed struct` entries), and the 8259 PIC/PIT config
// that makes the timer/keyboard lines usable. The entry stubs themselves
// (isr0/isr13/isr14/irq0/irq1) are hand-written in interrupts.s - saving/
// restoring full register state and normalizing "sometimes the CPU
// pushes an error code, sometimes it doesn't" is below what a MiniC
// function body can express - so they're declared `extern` here and
// just wired into the IDT by address.

import "io.mc";

packed struct idt_entry {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 zero;
}

packed struct idt_pointer {
    u16 limit;
    u64 base;
}

extern void isr0();
extern void isr13();
extern void isr14();
extern void irq0();
extern void irq1();
extern void isr_syscall();

idt_entry g_idt[256];
idt_pointer g_idt_ptr;

// `dpl` is the *minimum* privilege level allowed to reach this gate via
// a software `int n` - 0 for everything hardware-raised (exceptions,
// IRQs - ring3 code should never trigger those directly), 3 for the
// syscall gate specifically, since that's the whole point of it.
void set_idt_entry(int vector, u64 handler_addr, u8 dpl) {
    g_idt[vector].offset_low = (u16) handler_addr;
    g_idt[vector].selector = 0x08;    // the code64 selector from boot.s's GDT
    g_idt[vector].ist = 0;
    g_idt[vector].type_attr = 0x8E | (dpl << 5);   // present, DPL, 64-bit interrupt gate
    g_idt[vector].offset_mid = (u16) (handler_addr >> 16);
    g_idt[vector].offset_high = (u32) (handler_addr >> 32);
    g_idt[vector].zero = 0;
}

void idt_init() {
    set_idt_entry(0, (u64) &isr0, 0);
    set_idt_entry(13, (u64) &isr13, 0);
    set_idt_entry(14, (u64) &isr14, 0);
    set_idt_entry(32, (u64) &irq0, 0);
    set_idt_entry(33, (u64) &irq1, 0);
    set_idt_entry(0x80, (u64) &isr_syscall, 3);

    idt_entry* base = g_idt;
    g_idt_ptr.limit = (u16) (sizeof(idt_entry) * 256 - 1);
    g_idt_ptr.base = (u64) base;
    // Unlike boot.s's `lgdt` (which runs before the jump to long mode, so
    // it reads the 32-bit GDTR format), this `lidt` runs after we're
    // already in 64-bit mode, so g_idt_ptr's full 10-byte layout applies.
    asm("lidt [rip+g_idt_ptr]");
}

// 8259 PIC: remapped off the CPU's own exception vectors (0-31, where
// IRQ0-7 collide by default) onto 32-47, with only the timer/keyboard
// lines (IRQ0/IRQ1) unmasked - nothing else is handled yet.
void pic_remap() {
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
void pit_init() {
    u16 divisor = 11932;
    outb(0x43, 0x36);
    outb(0x40, (u8) divisor);
    outb(0x40, (u8) (divisor >> 8));
}
