// The MiniC side of the kernel. By the time boot.s's `call _start` reaches
// here, we're in 64-bit long mode with a real stack and the first 1GB of
// physical memory identity-mapped - including the VGA text buffer at
// 0xB8000, which needs no driver at all: it's just memory.
//
// Just the entry point now - everything else lives in its own module,
// one per subsystem (io/interrupts/keyboard/heap/frames/paging/strings/
// isr/shell). See README.md for the full milestone-by-milestone writeup
// of what each one does and why.

import "io.mc";
import "interrupts_init.mc";
import "keyboard.mc";
import "frames.mc";
import "paging.mc";
import "shell.mc";
import "isr.mc";

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
    readPML4();
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
