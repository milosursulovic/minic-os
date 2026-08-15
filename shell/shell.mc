// The minimal interactive shell, built on keyboard.mc's line buffer.
// The main loop (kmain.mc's _start) dispatches a line via runCommand()
// once keyboard.mc's IRQ1 handler (isr.mc) sets gLineReady.

import "../drivers/io.mc";
import "../lib/strings.mc";
import "../mm/heap.mc";
import "../mm/frames.mc";
import "../mm/paging.mc";
import "../drivers/keyboard.mc";
import "../isr/isr.mc";

void printPrompt() {
    vgaPrint("> ");
    serialPrint("> ");
}

void cmdHelp() {
    vgaPrint("commands: help clear ticks alloc free free <addr> mem reset frame unframe frames map echo <text>");
    serialPrint("commands: help clear ticks alloc free free <addr> mem reset frame unframe frames map echo <text>\n");
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
    serialPrint("ticks: 0x");
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
        gLastFrame = f;
        vgaPrint("allocated frame at 0x");
        serialPrint("allocated frame at 0x");
        printHex((u64) f);
    }
}

void cmdUnframe() {
    if (gLastFrame == null) {
        vgaPrint("nothing to unframe");
        serialPrint("nothing to unframe\n");
        return;
    }
    vgaPrint("freed frame 0x");
    serialPrint("freed frame 0x");
    printHex((u64) gLastFrame);
    freeFrame(gLastFrame);
    gLastFrame = null;
}

void cmdMap() {
    void* frame = allocFrame();
    if (frame == null) {
        vgaPrint("out of frames");
        serialPrint("out of frames\n");
        return;
    }
    u64 vaddr = 0x40000000;   // 1GB - just past boot.s's static identity map
    bool ok = mapPage(vaddr, (u64) frame, 0x02);   // writable
    if (!ok) {
        vgaPrint("map failed");
        serialPrint("map failed\n");
        freeFrame(frame);
        return;
    }

    u32* p = (u32*) vaddr;
    p[0] = 0xCAFEBABE;
    u32 readBack = p[0];

    vgaPrint("mapped 0x");
    serialPrint("mapped 0x");
    printHex(vaddr);
    vgaPrint(" -> 0x");
    serialPrint(" -> 0x");
    printHex((u64) frame);
    vgaPrint(", wrote/read 0x");
    serialPrint(", wrote/read 0x");
    printHex((u64) readBack);
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
    } else if (streq(gLineBuffer, "unframe")) {
        cmdUnframe();
    } else if (streq(gLineBuffer, "map")) {
        cmdMap();
    } else if (startsWith(gLineBuffer, "echo ")) {
        cmdEcho();
    } else if (gLineLen > 0) {
        vgaPrint("unknown command");
        serialPrint("unknown command\n");
    }
}
