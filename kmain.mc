// The MiniC side of the kernel. By the time boot.s's `call _start` reaches
// here, we're in 64-bit long mode with a real stack and the first 1GB of
// physical memory identity-mapped - including the VGA text buffer at
// 0xB8000, which needs no driver at all: it's just memory.
//
// Just the entry point now - everything else lives in its own module,
// one per subsystem (io/interrupts/keyboard/heap/frames/paging/strings/
// isr/shell). See README.md for the full milestone-by-milestone writeup
// of what each one does and why.

import "drivers/io.mc";
import "drivers/interrupts_init.mc";
import "drivers/keyboard.mc";
import "mm/frames.mc";
import "mm/paging.mc";
import "shell/shell.mc";
import "isr/isr.mc";
import "sched/task.mc";
import "syscall/syscall.mc";
import "proc/process.mc";
import "disk/ata.mc";
import "disk/minifs.mc";
import "disk/vfs.mc";

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

    // Scheduler state must exist *before* interrupts are live - the timer
    // ISR calls yield() unconditionally now (milestone 9's preemption),
    // and yield() divides by gTaskCount; a timer tick landing between
    // `sti` and schedulerInit() would hit gTaskCount==0.
    schedulerInit();
    createTask(&task1Entry);
    createTask(&task2Entry);
    createTask(&task3Entry);
    createTask(&task4Entry);
    createIsolatedTask(&procAEntry);
    createIsolatedTask(&procBEntry);
    // gChannelDemo (milestone 15) MUST be created first so it keeps
    // channel index 0, exactly as every existing test/transcript already
    // assumes - createChannel() just returns gChannelCount at the time
    // of the call, so creation ORDER is what fixes each channel's index,
    // not which global variable it's assigned to.
    gChannelDemo = createChannel();
    // Milestone 23: a second, dedicated channel for the ring3 program's
    // own Channel.receive() call - index 1 by construction (the second
    // createChannel() call overall), created before spawnProcess() so it
    // already exists the instant that task starts running. Deliberately
    // a *different* channel than gChannelDemo's - the M15 kernel-task
    // demo and this ring3 demo must never share one, or the shell's
    // `send` command and this milestone's spawn-trigger command would
    // race on the same mailbox.
    gRing3ChannelDemo = createChannel();
    // Milestone 24: stackVaddr moved from 0x80001000 to 0x80020000 (128KB
    // of headroom past loadVaddr) - a real bug found this milestone, not
    // a style change. The compiled ring3 image crossed 4096 bytes (one
    // page) for the first time here; at the old stackVaddr, the image's
    // own second page and the user stack landed on the identical virtual
    // address, and whichever mapPageIn() call ran second silently won,
    // leaving the other unreachable at that address. See proc/ring3prog.mc's
    // header comment for the full account. Every spawnProcess()/
    // spawnProcessFromPath() call site (here, shell.mc's cmdSpawn, and
    // ring3prog.mc's own Process.spawn() call) must keep using this same
    // stackVaddr, not just this one - a mismatched constant would silently
    // reintroduce the exact same class of collision.
    spawnProcess(&gTestProgStart, &gTestProgEnd, 0x80000000, 0x80020000);
    createIsolatedTask(&procReceiverEntry);
    vfsMount("/system", BACKEND_MINIFS);
    vfsMount("/devices", BACKEND_DEVICE);
    asm("sti");

    serialPrint("interrupts live\n");
    printPrompt();

    while (true) {
        asm("hlt");   // the CPU sleeps here between interrupts; every timer
        // tick preempts whatever's running (including this loop) via
        // interrupt_handler's own yield() call now - no manual yield()
        // needed here anymore, milestone 8's cooperative one was folded
        // into that automatic preemption.
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
