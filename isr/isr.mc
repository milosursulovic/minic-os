// Called from interrupts.s's isr_common_stub for every vector it knows
// about. Real MiniC, called with an ordinary `call` - a normal function,
// nothing interrupt-specific about its body.
//
// minicc's unused-function warning can't see that call - interrupts.s is
// a separate, hand-written assembly file, never parsed by minicc at all,
// so a `warning: unused function 'interrupt_handler'` here is a known
// false positive, not a bug. Same blind spot real C compilers have for
// any non-`static` function - gcc's own -Wunused-function only fires for
// internal-linkage functions for exactly this reason (MiniC doesn't have
// an internal-linkage concept yet to make the same distinction). It also
// means kmain.mc must import this file even though no MiniC code calls
// interrupt_handler directly - interrupts.s's `call interrupt_handler`
// needs the symbol to exist in the compiled program at all.

import "../drivers/io.mc";
import "../drivers/keyboard.mc";
import "../mm/paging.mc";
import "../lib/strings.mc";
import "../sched/task.mc";

u64 gTickCount;

void interrupt_handler(u64 vector, u64 errorCode) {
    if (vector == 32) {
        gTickCount = gTickCount + 1;
        if (gTickCount % 100 == 0) {
            serialPutc('.');   // one dot per ~1 second at 100Hz, proves the timer keeps firing
        }
        outb(0x20, 0x20);      // EOI - always send this before yield() might switch away,
        // so the PIC gets acknowledged regardless of which task ends up resuming here

        // Preemption: yield() is the exact same cooperative switch task.mc's
        // demo tasks call voluntarily - calling it from *inside* the timer
        // ISR is what makes it preemptive instead. It works because
        // interrupt_handler is just an ordinary nested function call on
        // whichever task's stack the CPU happened to interrupt: when
        // switch_context() saves this task's rsp and jumps to the next
        // task's, this call is simply suspended mid-call, exactly like a
        // voluntary yield() would suspend it - the timer interrupt's full
        // trap frame (pushed by interrupts.s below this point on the
        // stack) rides along for free and gets `iretq`'d correctly once
        // the ring cascades back around to this exact call and it returns
        // normally. See yield()'s own comment for the one real gap this
        // doesn't handle for free (the IF flag).
        yield();
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
        readCr2();
        serialPrint("page fault at 0x");
        printHex(gCr2Value);
        serialPrint(", halting\n");
    } else {
        serialPrint("unhandled exception, halting\n");
    }
    while (true) {
        asm("hlt");
    }
}
