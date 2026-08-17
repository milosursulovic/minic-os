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
import "../sched/task.mc";
import "../syscall/syscall.mc";
import "../proc/process.mc";
import "../proc/object.mc";
import "../proc/channel.mc";
import "../disk/ata.mc";
import "../disk/minifs.mc";
import "../disk/vfs.mc";
import "../drivers/pci.mc";
import "../net/e1000.mc";

void printPrompt() {
    vgaPrint("> ");
    serialPrint("> ");
}

void cmdHelp() {
    vgaPrint("commands: help clear ticks alloc bigalloc free free <addr> mem reset frame unframe frames map tasks procs ps objs chan send pci nic arp disk diskwrite mkfs mkfile cat ls vfscat <path> vfswrite install spawn ring3go ring3fault ring3nx echo <text>");
    serialPrint("commands: help clear ticks alloc bigalloc free free <addr> mem reset frame unframe frames map tasks procs ps objs chan send pci nic arp disk diskwrite mkfs mkfile cat ls vfscat <path> vfswrite install spawn ring3go ring3fault ring3nx echo <text>\n");
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

void cmdBigAlloc() {
    // 64KB - bigger than a single heapGrow() chunk, so this reliably
    // forces at least one on-demand growth cycle in one shot, instead of
    // needing dozens of plain `alloc`s to exhaust the initial mapping.
    void* p = kalloc(65536);
    if (p == null) {
        vgaPrint("bigalloc failed - heap full");
        serialPrint("bigalloc failed - heap full\n");
    } else {
        gLastAlloc = p;
        vgaPrint("allocated 65536 bytes at 0x");
        serialPrint("allocated 65536 bytes at 0x");
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
    vgaPrint(" / 0x");
    serialPrint(" / 0x");
    printHex(gHeapSize);
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
    bool ok = mapPage(vaddr, (u64) frame, 0x02 | PAGE_NX);   // writable, non-executable (milestone 28)
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

void cmdTasks() {
    vgaPrint("task1: 0x");
    serialPrint("task1: 0x");
    printHex(gTask1Ticks);
    vgaPrint(" task2: 0x");
    serialPrint(" task2: 0x");
    printHex(gTask2Ticks);
    vgaPrint(" task3: 0x");
    serialPrint(" task3: 0x");
    printHex(gTask3Ticks);
    vgaPrint(" task4: 0x");
    serialPrint(" task4: 0x");
    printHex(gTask4Ticks);
    vgaPrint(" ticks: 0x");
    serialPrint(" ticks: 0x");
    printHex(gTickCount);
}

// Milestone 29: the first time this kernel discovers its own hardware
// instead of trusting a hardcoded I/O port. Walks bus 0's 32 device
// slots via the legacy CONFIG_ADDRESS/CONFIG_DATA mechanism and prints
// every real device found - independently checkable against QEMU's own
// default machine model (a host bridge, an ISA bridge, an IDE
// controller, usually a VGA device and a default NIC), not something
// this kernel can fake or hardcode its way into looking right.
void cmdPci() {
    pciEnumerate();
    vgaPrint("pci devices: 0x");
    serialPrint("pci devices: 0x");
    printHex((u64) gPciDeviceCount);
    int i = 0;
    while (i < gPciDeviceCount) {
        PciDevice* d = &gPciDevices[i];
        vgaPrint(" ");
        serialPrint(" ");
        printHex((u64) d->bus);
        vgaPrint(":");
        serialPrint(":");
        printHex((u64) d->device);
        vgaPrint(".");
        serialPrint(".");
        printHex((u64) d->function);
        vgaPrint(" vendor=0x");
        serialPrint(" vendor=0x");
        printHex((u64) d->vendorId);
        vgaPrint(" device=0x");
        serialPrint(" device=0x");
        printHex((u64) d->deviceId);
        vgaPrint(" class=0x");
        serialPrint(" class=0x");
        printHex((u64) d->classCode);
        vgaPrint(" subclass=0x");
        serialPrint(" subclass=0x");
        printHex((u64) d->subclass);
        i = i + 1;
    }
}

// Milestone 30: initializes the e1000 NIC milestone 29's `pci` command
// already found (enables it over PCI, maps its MMIO register file) and
// reads back real hardware state - its actual MAC address (from RAL0/
// RAH0, pre-loaded by QEMU's emulated EEPROM the same way real hardware
// auto-loads its burned-in address) and its link-up status. Read-only
// proof only - no packet has been sent or received yet, see the
// roadmap for what's still ahead in this phase.
void cmdNic() {
    bool ok = e1000Init();
    if (!ok) {
        vgaPrint("e1000 init failed - device not found at 0:3.0");
        serialPrint("e1000 init failed - device not found at 0:3.0\n");
        return;
    }
    u8 mac[6];
    e1000GetMac(&mac[0]);
    vgaPrint("e1000 mac=");
    serialPrint("e1000 mac=");
    int i = 0;
    while (i < 6) {
        printHex((u64) mac[i]);
        if (i < 5) {
            vgaPrint(":");
            serialPrint(":");
        }
        i = i + 1;
    }
    bool linkUp = e1000LinkUp();
    vgaPrint(" linkUp=0x");
    serialPrint(" linkUp=0x");
    printHex((u64) linkUp);
}

// Milestone 31: real TX/RX descriptor rings and a genuine, end-to-end
// packet round trip - the DMA-buffer-management work milestone 30
// deliberately deferred. Deliberately NOT a real ARP subsystem (no
// address resolution cache, no general request/reply handling) - this
// crafts exactly ONE hardcoded, valid Ethernet+ARP request frame,
// purely as a real stimulus to prove RX genuinely works, the same way
// milestone 26's `ring3fault` crafted one hardcoded forbidden access
// rather than building a general fault-injection framework. Asks "who
// has 10.0.2.2" (QEMU user-mode networking's own well-known default
// gateway address) - if QEMU's SLIRP backend replies, that's a REAL
// external round trip, not a loopback or something this driver could
// fake on its own.
void cmdArp() {
    bool ok = e1000Init();
    if (!ok) {
        vgaPrint("e1000 init failed - device not found at 0:3.0");
        serialPrint("e1000 init failed - device not found at 0:3.0\n");
        return;
    }
    if (!e1000InitRings()) {
        vgaPrint("e1000 ring setup failed - out of frames");
        serialPrint("e1000 ring setup failed - out of frames\n");
        return;
    }

    u8 mac[6];
    e1000GetMac(&mac[0]);

    u8 frame[60];
    int i = 0;
    while (i < 60) {
        frame[i] = 0;
        i = i + 1;
    }
    // dest = broadcast
    i = 0;
    while (i < 6) {
        frame[i] = 0xFF;
        i = i + 1;
    }
    // src = our real MAC
    i = 0;
    while (i < 6) {
        frame[6 + i] = mac[i];
        i = i + 1;
    }
    frame[12] = 0x08;   // EtherType = 0x0806 (ARP), big-endian on the wire
    frame[13] = 0x06;
    frame[14] = 0x00;   // hwtype = 1 (Ethernet)
    frame[15] = 0x01;
    frame[16] = 0x08;   // ptype = 0x0800 (IPv4)
    frame[17] = 0x00;
    frame[18] = 6;       // hwlen
    frame[19] = 4;       // protolen
    frame[20] = 0x00;   // opcode = 1 (request)
    frame[21] = 0x01;
    i = 0;
    while (i < 6) {
        frame[22 + i] = mac[i];   // sender MAC = ours
        i = i + 1;
    }
    frame[28] = 10;   // sender IP = 10.0.2.15 (QEMU SLIRP's default guest address)
    frame[29] = 0;
    frame[30] = 2;
    frame[31] = 15;
    // target MAC left zeroed (unknown - being resolved)
    frame[38] = 10;   // target IP = 10.0.2.2 (QEMU SLIRP's default gateway)
    frame[39] = 0;
    frame[40] = 2;
    frame[41] = 2;

    bool sent = e1000Send(&frame[0], 60);
    vgaPrint("arp request sent=0x");
    serialPrint("arp request sent=0x");
    printHex((u64) sent);
    if (!sent) {
        return;
    }

    // A real external reply (through QEMU's SLIRP backend) takes real
    // wall-clock time - poll against the kernel's own tick counter
    // (isr.mc's gTickCount, ~100Hz nominal though QEMU/TCG runs it
    // faster in practice) rather than trusting an instruction-count
    // spin to correspond to any particular amount of real time.
    u8 reply[64];
    u16 replyLen = 0;
    u64 startTick = gTickCount;
    while (gTickCount - startTick < 2000) {
        replyLen = e1000Receive(&reply[0], 64);
        if (replyLen > 0) {
            break;
        }
    }
    vgaPrint(" reply len=0x");
    serialPrint(" reply len=0x");
    printHex((u64) replyLen);
    if (replyLen == 0) {
        return;
    }

    bool isArp = reply[12] == 0x08 && reply[13] == 0x06;
    bool isReply = reply[20] == 0x00 && reply[21] == 0x02;
    vgaPrint(" isArpReply=0x");
    serialPrint(" isArpReply=0x");
    printHex((u64) (isArp && isReply));
    if (isArp && isReply) {
        vgaPrint(" from=");
        serialPrint(" from=");
        i = 0;
        while (i < 6) {
            printHex((u64) reply[22 + i]);
            if (i < 5) {
                vgaPrint(":");
                serialPrint(":");
            }
            i = i + 1;
        }
        vgaPrint(" senderIp=");
        serialPrint(" senderIp=");
        printHex((u64) reply[28]);
        vgaPrint(".");
        serialPrint(".");
        printHex((u64) reply[29]);
        vgaPrint(".");
        serialPrint(".");
        printHex((u64) reply[30]);
        vgaPrint(".");
        serialPrint(".");
        printHex((u64) reply[31]);
    }
}

// Reads LBA 1, a sector the disk image is pre-populated with (from the
// host side, before boot - see build.sh's disk-image step) with a known
// ASCII signature followed by zero-fill. Printing it as a string is
// safe precisely because of that zero-fill: the byte right after the
// signature is a real null terminator, not luck.
void cmdDisk() {
    u8 buf[512];
    bool ok = ataReadSector(1, buf);
    if (!ok) {
        vgaPrint("disk read failed");
        serialPrint("disk read failed\n");
        return;
    }
    char* s = (char*) &buf[0];
    vgaPrint("sector 1: ");
    serialPrint("sector 1: ");
    vgaPrint(s);
    serialPrint(s);
}

// Writes a fixed pattern to LBA 100 (arbitrary, clear of the signature
// sector) and immediately reads it back into a SEPARATE buffer -
// comparing the two proves a real round trip through the driver, not
// just "a write instruction executed" or "a read instruction executed."
void cmdDiskWrite() {
    u8 writeBuf[512];
    int i = 0;
    while (i < 512) {
        writeBuf[i] = (u8) (i & 0xFF);
        i = i + 1;
    }
    bool wrote = ataWriteSector(100, writeBuf);
    if (!wrote) {
        vgaPrint("disk write failed");
        serialPrint("disk write failed\n");
        return;
    }
    u8 readBuf[512];
    bool read = ataReadSector(100, readBuf);
    if (!read) {
        vgaPrint("disk write ok, readback failed");
        serialPrint("disk write ok, readback failed\n");
        return;
    }
    bool match = true;
    i = 0;
    while (i < 512) {
        if (writeBuf[i] != readBuf[i]) {
            match = false;
        }
        i = i + 1;
    }
    if (match) {
        vgaPrint("write+readback verified, 512/512 bytes match");
        serialPrint("write+readback verified, 512/512 bytes match\n");
    } else {
        vgaPrint("MISMATCH - write or read is broken");
        serialPrint("MISMATCH - write or read is broken\n");
    }
}

void cmdMkfs() {
    bool ok = mkfs();
    if (ok) {
        vgaPrint("filesystem formatted");
        serialPrint("filesystem formatted\n");
    } else {
        vgaPrint("mkfs failed");
        serialPrint("mkfs failed\n");
    }
}

int gNextFileIndex;
char gLastFileName[20];

// Creates a new file every call, name and content both embedding the
// same running index ("file0.mfs" / "file1.mfs" / ...) - running this
// twice and `cat`-ing after each one is what proves multiple files
// coexist correctly (distinct content, no overwriting each other) and
// that the free-space scan advances past each file already written.
void cmdMkfile() {
    char nameBuf[20];
    nameBuf[0] = 'f'; nameBuf[1] = 'i'; nameBuf[2] = 'l'; nameBuf[3] = 'e';
    nameBuf[4] = (char) ('0' + (u8) (gNextFileIndex % 10));
    nameBuf[5] = '.'; nameBuf[6] = 'm'; nameBuf[7] = 'f'; nameBuf[8] = 's';
    nameBuf[9] = '\0';

    char contentBuf[64];
    char* prefix = "Hello from MiniFS, this is file #";
    int i = 0;
    while (prefix[i] != '\0') {
        contentBuf[i] = prefix[i];
        i = i + 1;
    }
    contentBuf[i] = (char) ('0' + (u8) (gNextFileIndex % 10));
    i = i + 1;
    contentBuf[i] = '\0';
    i = i + 1;

    bool ok = fsWriteFile(nameBuf, (u8*) &contentBuf[0], (u32) i);
    if (!ok) {
        vgaPrint("mkfile failed");
        serialPrint("mkfile failed\n");
        return;
    }
    copyName(&gLastFileName[0], nameBuf);
    gNextFileIndex = gNextFileIndex + 1;
    vgaPrint("created ");
    serialPrint("created ");
    vgaPrint(nameBuf);
    serialPrint(nameBuf);
}

void cmdCat() {
    if (gLastFileName[0] == '\0') {
        vgaPrint("no file yet - run mkfile first");
        serialPrint("no file yet - run mkfile first\n");
        return;
    }
    u8 buf[65];
    int n = fsReadFile(&gLastFileName[0], buf, 64);
    if (n < 0) {
        vgaPrint("cat failed");
        serialPrint("cat failed\n");
        return;
    }
    buf[n] = 0;   // fsWriteFile's own null terminator is part of the stored bytes already, this is just defensive
    char* s = (char*) &buf[0];
    vgaPrint(s);
    serialPrint(s);
}

void cmdLs() {
    u8 sbBuf[512];
    if (!ataReadSector(SUPERBLOCK_LBA, sbBuf)) {
        vgaPrint("ls failed - disk read error");
        serialPrint("ls failed - disk read error\n");
        return;
    }
    Superblock* sb = (Superblock*) &sbBuf[0];
    vgaPrint("fileCount: 0x");
    serialPrint("fileCount: 0x");
    printHex((u64) sb->fileCount);
    vgaPrint("  ");
    serialPrint("  ");

    u8 dirBuf[512];
    bool ok = ataReadSector(DIRECTORY_LBA, dirBuf);
    if (!ok) {
        vgaPrint("ls failed - disk read error");
        serialPrint("ls failed - disk read error\n");
        return;
    }
    DirEntry* entries = (DirEntry*) &dirBuf[0];
    int i = 0;
    int shown = 0;
    while (i < (int) MAX_FILES) {
        if (entries[i].used) {
            vgaPrint(entries[i].name);
            serialPrint(entries[i].name);
            vgaPrint(" 0x");
            serialPrint(" 0x");
            printHex((u64) entries[i].sizeBytes);
            vgaPrint("  ");
            serialPrint("  ");
            shown = shown + 1;
        }
        i = i + 1;
    }
    if (shown == 0) {
        vgaPrint("(empty)");
        serialPrint("(empty)");
    }
}

// Reads an arbitrary path through the VFS - "vfscat /system/file0.mfs"
// routes to MiniFS (real disk I/O), "vfscat /devices/ticks" routes to
// devfs (live kernel state, no disk touched at all). Same function
// call, two completely different mechanisms depending only on the path
// prefix - that's the actual milestone 18 proof, not the printed text.
void cmdVfsCat() {
    char* path = &gLineBuffer[7];   // past "vfscat "
    u8 buf[256];
    int n = vfsRead(path, buf, 256);
    if (n == -2) {
        vgaPrint("vfscat: file too large to display");
        serialPrint("vfscat: file too large to display\n");
        return;
    }
    if (n < 0) {
        vgaPrint("vfscat: not found");
        serialPrint("vfscat: not found\n");
        return;
    }
    buf[n] = 0;
    char* s = (char*) &buf[0];
    vgaPrint(s);
    serialPrint(s);
}

// Writes a fixed demo file through the VFS (not fsWriteFile() directly)
// to prove the write side routes too, not just reads - `vfscat` the
// same path afterward, or plain `ls` (MiniFS's own directory listing,
// unaware this file arrived via VFS rather than `mkfile`) to see it
// landed in the exact same underlying MiniFS directory.
void cmdVfsWrite() {
    char* content = "This file was written through the VFS layer, not MiniFS directly.";
    int len = strlen(content) + 1;   // include the null terminator, same as mkfile's content
    bool ok = vfsWrite("/system/vfsdemo.mfs", (u8*) content, (u32) len);
    if (ok) {
        vgaPrint("wrote /system/vfsdemo.mfs via VFS");
        serialPrint("wrote /system/vfsdemo.mfs via VFS\n");
    } else {
        vgaPrint("vfswrite failed");
        serialPrint("vfswrite failed\n");
    }
}

// Milestone 19 setup step: writes the kernel's own compiled-in test
// program (proc/testprog.s, gTestProgStart..gTestProgEnd - the exact
// same bytes milestone 13's boot-time spawnProcess() already uses) out
// to a real MiniFS file, simulating "this program is now genuinely
// installed on disk," addressable by path and indistinguishable from
// any other file - not a special-cased kernel-image blob anymore from
// this point on. `spawn` is what actually proves the load-from-disk
// path; `install` just gets real bytes onto real storage first.
void cmdInstall() {
    u32 len = (u32) ((u64) &gTestProgEnd - (u64) &gTestProgStart);
    bool ok = vfsWrite("/system/testprog.bin", &gTestProgStart, len);
    if (!ok) {
        vgaPrint("install failed");
        serialPrint("install failed\n");
        return;
    }
    vgaPrint("installed /system/testprog.bin, 0x");
    serialPrint("installed /system/testprog.bin, 0x");
    printHex((u64) len);
    vgaPrint(" bytes");
    serialPrint(" bytes");
}

// The milestone 19 proof: reads /system/testprog.bin back through the
// VFS and spawns a brand-new isolated ring3 process from THOSE bytes -
// a second, independent instance of the same program, loaded from disk
// this time rather than the kernel's own compiled-in image. Reuses the
// exact same load virtual address procA/procB/the milestone-13 process
// all use (0x80000000) - safe, since this process gets its own freshly
// cloned address space, same as every isolated task before it.
// stackVaddr is 0x80020000, not 0x80001000 - see kmain.mc's comment on
// its own spawnProcess() call for the real milestone-24 bug this fixes;
// every spawn call site must agree on this same constant.
void cmdSpawn() {
    int idx = spawnProcessFromPath("/system/testprog.bin", 0x80000000, 0x80020000);
    if (idx < 0) {
        vgaPrint("spawn failed");
        serialPrint("spawn failed\n");
        return;
    }
    vgaPrint("spawned process 0x");
    serialPrint("spawned process 0x");
    printHex((u64) idx);
}

void cmdChan() {
    vgaPrint("receiver got: 0x");
    serialPrint("receiver got: 0x");
    printHex((u64) gReceiverGotMessage);
    vgaPrint(" value=0x");
    serialPrint(" value=0x");
    printHex(gReceiverValue);
}

void cmdSend() {
    bool ok = channelSend(gChannelDemo, 0xC0FFEE1234);
    if (!ok) {
        vgaPrint("send failed - channel full");
        serialPrint("send failed - channel full\n");
        return;
    }
    vgaPrint("sent 0xc0ffee1234");
    serialPrint("sent 0xc0ffee1234\n");
}

// Milestone 23: wakes the boot-time ring3 process's own blocking
// Channel.receive() call (a real ring3 syscall, not this kernel task
// calling channelReceive() directly the way procReceiverEntry does) -
// operator-triggered on purpose, same "deterministic, not racing the
// timer" reasoning as `send` above. Once the ring3 process receives
// this, it goes on to call Process.spawn() - run `install` first so
// the file it spawns actually exists on disk.
void cmdRing3Go() {
    bool ok = channelSend(gRing3ChannelDemo, 0x1);
    if (!ok) {
        vgaPrint("ring3go failed - channel full");
        serialPrint("ring3go failed - channel full\n");
        return;
    }
    vgaPrint("sent ring3 spawn trigger");
    serialPrint("sent ring3 spawn trigger\n");
}

// Milestone 26: sends trigger value 0x2 (distinct from ring3go's 0x1) on
// the same gRing3ChannelDemo mailbox - the boot-time ring3 process
// branches on this to attempt a deliberate forbidden write instead of
// spawning. This is a ONE-SHOT, KERNEL-HALTING command: if the fix in
// mm/paging.mc's cloneAddressSpace() is working, the write takes a real
// page fault and the kernel halts right there (isr.mc's existing
// handler, no new kernel code needed) - run it in its own dedicated
// session, never interleaved with other regression testing.
void cmdRing3Fault() {
    bool ok = channelSend(gRing3ChannelDemo, 0x2);
    if (!ok) {
        vgaPrint("ring3fault failed - channel full");
        serialPrint("ring3fault failed - channel full\n");
        return;
    }
    vgaPrint("sent ring3 forbidden-write trigger - expect a page fault");
    serialPrint("sent ring3 forbidden-write trigger - expect a page fault\n");
}

// Milestone 28: sends trigger value 0x3 (distinct from ring3go's 0x1 and
// ring3fault's 0x2) on the same mailbox - the boot-time ring3 process
// branches on this to write a `ret` opcode onto its own user stack and
// attempt to execute it, proving PAGE_NX (mm/paging.mc) really is
// enforced there. Also ONE-SHOT and KERNEL-HALTING, same caveat as
// ring3fault - run it in its own dedicated session.
void cmdRing3Nx() {
    bool ok = channelSend(gRing3ChannelDemo, 0x3);
    if (!ok) {
        vgaPrint("ring3nx failed - channel full");
        serialPrint("ring3nx failed - channel full\n");
        return;
    }
    vgaPrint("sent ring3 stack-execution trigger - expect a page fault");
    serialPrint("sent ring3 stack-execution trigger - expect a page fault\n");
}

void cmdProcs() {
    vgaPrint("procA: 0x");
    serialPrint("procA: 0x");
    printHex((u64) gProcAValue);
    vgaPrint(" @phys 0x");
    serialPrint(" @phys 0x");
    printHex(gProcAPhys);
    vgaPrint(" procB: 0x");
    serialPrint(" procB: 0x");
    printHex((u64) gProcBValue);
    vgaPrint(" @phys 0x");
    serialPrint(" @phys 0x");
    printHex(gProcBPhys);
}

// Milestone 19 grew this from "print process 0's details" to a real
// loop over every process - the moment more than one could ever exist
// (spawn), showing only the first stopped being useful for confirming
// each spawned process is genuinely distinct (different task, possibly
// different cr3) rather than the same one reported twice.
void cmdPs() {
    vgaPrint("processes: 0x");
    serialPrint("processes: 0x");
    printHex((u64) gProcessCount);
    int i = 0;
    while (i < gProcessCount) {
        Process* p = &gProcesses[i];
        vgaPrint(" proc");
        serialPrint(" proc");
        printHex((u64) i);
        vgaPrint(" task=0x");
        serialPrint(" task=0x");
        printHex((u64) p->taskIndex);
        vgaPrint(" cr3=0x");
        serialPrint(" cr3=0x");
        printHex(p->cr3);
        i = i + 1;
    }
}

void cmdObjs() {
    vgaPrint("objects: 0x");
    serialPrint("objects: 0x");
    printHex((u64) gObjectCount);
    if (gObjectCount > 0) {
        vgaPrint(" obj0 type=0x");
        serialPrint(" obj0 type=0x");
        printHex((u64) gObjects[0].type);
        vgaPrint(" dataIndex=0x");
        serialPrint(" dataIndex=0x");
        printHex((u64) gObjects[0].dataIndex);
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
    } else if (streq(gLineBuffer, "bigalloc")) {
        cmdBigAlloc();
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
    } else if (streq(gLineBuffer, "tasks")) {
        cmdTasks();
    } else if (streq(gLineBuffer, "procs")) {
        cmdProcs();
    } else if (streq(gLineBuffer, "ps")) {
        cmdPs();
    } else if (streq(gLineBuffer, "objs")) {
        cmdObjs();
    } else if (streq(gLineBuffer, "chan")) {
        cmdChan();
    } else if (streq(gLineBuffer, "send")) {
        cmdSend();
    } else if (streq(gLineBuffer, "ring3go")) {
        cmdRing3Go();
    } else if (streq(gLineBuffer, "ring3fault")) {
        cmdRing3Fault();
    } else if (streq(gLineBuffer, "ring3nx")) {
        cmdRing3Nx();
    } else if (streq(gLineBuffer, "pci")) {
        cmdPci();
    } else if (streq(gLineBuffer, "nic")) {
        cmdNic();
    } else if (streq(gLineBuffer, "arp")) {
        cmdArp();
    } else if (streq(gLineBuffer, "disk")) {
        cmdDisk();
    } else if (streq(gLineBuffer, "diskwrite")) {
        cmdDiskWrite();
    } else if (streq(gLineBuffer, "mkfs")) {
        cmdMkfs();
    } else if (streq(gLineBuffer, "mkfile")) {
        cmdMkfile();
    } else if (streq(gLineBuffer, "cat")) {
        cmdCat();
    } else if (streq(gLineBuffer, "ls")) {
        cmdLs();
    } else if (startsWith(gLineBuffer, "vfscat ")) {
        cmdVfsCat();
    } else if (streq(gLineBuffer, "vfswrite")) {
        cmdVfsWrite();
    } else if (streq(gLineBuffer, "install")) {
        cmdInstall();
    } else if (streq(gLineBuffer, "spawn")) {
        cmdSpawn();
    } else if (startsWith(gLineBuffer, "echo ")) {
        cmdEcho();
    } else if (gLineLen > 0) {
        vgaPrint("unknown command");
        serialPrint("unknown command\n");
    }
}
