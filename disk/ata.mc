// Milestone 16: a legacy ATA (IDE) PIO driver - the first real storage
// I/O this kernel has ever done. Same hand-rolled, direct-port-I/O style
// as every earlier driver (VGA/keyboard/PIT): no libc, no BIOS calls,
// just the raw registers a real IDE controller exposes at the classic
// ISA port addresses every PC-compatible (including QEMU's default
// machine type) still wires up for backward compatibility. AHCI/NVMe
// (real modern storage) and PCI enumeration to find them are explicitly
// later work - see README's roadmap; PIO is the simplest thing that can
// prove real disk I/O works at all, the same reasoning that put a bump
// allocator before a free-list heap and cooperative scheduling before
// preemptive.
//
// Deliberately polling, not interrupt-driven - one fewer moving part
// while proving the basic read/write path works, matching how milestone
// 2's interrupts came before milestone 3's shell rather than the two
// being tackled together.

import "../drivers/io.mc";

u16 ata_data = 0x1F0;
u16 ata_sector_count = 0x1F2;
u16 ata_lba_low = 0x1F3;
u16 ata_lba_mid = 0x1F4;
u16 ata_lba_high = 0x1F5;
u16 ata_drive_head = 0x1F6;
u16 ata_status = 0x1F7;
u16 ata_command = 0x1F7;

u8 ata_status_err = 0x01;
u8 ata_status_drq = 0x08;
u8 ata_status_bsy = 0x80;

u8 ata_cmd_read = 0x20;
u8 ata_cmd_write = 0x30;
u8 ata_cmd_flush = 0xE7;

// Bounded, not an unconditional `while (busy) {}` - a real IDE
// controller answers in microseconds, so hitting this cap means
// something is genuinely wrong (no disk attached, wrong port, a bad
// QEMU `-drive` flag) and the caller should get a clean failure back
// instead of the kernel hanging forever waiting on hardware that isn't
// there. The exact iteration count is arbitrary, just generously past
// how many spins a real answer ever takes.
bool ata_wait_ready() {
    u32 spins = 0;
    while (spins < 1000000) {
        if ((inb(ata_status) & ata_status_bsy) == 0) {
            return true;
        }
        spins = spins + 1;
    }
    return false;
}

bool ata_wait_drq() {
    u32 spins = 0;
    while (spins < 1000000) {
        u8 status = inb(ata_status);
        if ((status & ata_status_err) != 0) {
            return false;
        }
        if ((status & ata_status_bsy) == 0 && (status & ata_status_drq) != 0) {
            return true;
        }
        spins = spins + 1;
    }
    return false;
}

// Selects the primary master drive in LBA28 mode and loads a 28-bit
// sector address + count into the command block - shared setup for
// both read and write, which differ only in the command byte and which
// direction the data port gets used.
bool ata_setup(u32 lba, u8 sector_count) {
    if (!ata_wait_ready()) {
        return false;
    }
    // 0xE0: LBA mode + drive 0 (master), ORed with LBA bits 24-27.
    outb(ata_drive_head, (u8) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(ata_sector_count, sector_count);
    outb(ata_lba_low, (u8) (lba & 0xFF));
    outb(ata_lba_mid, (u8) ((lba >> 8) & 0xFF));
    outb(ata_lba_high, (u8) ((lba >> 16) & 0xFF));
    return true;
}

// Reads exactly one 512-byte sector into buffer (must have room for
// 512 bytes). Transfers 256 16-bit words, not 512 bytes one at a time -
// the data port is genuinely 16 bits wide on real ATA hardware, which
// is why this driver needed drivers/io.mc's new outw/inw.
bool ata_read_sector(u32 lba, u8* buffer) {
    if (!ata_setup(lba, 1)) {
        return false;
    }
    outb(ata_command, ata_cmd_read);
    if (!ata_wait_drq()) {
        return false;
    }
    u16* words = (u16*) buffer;
    int i = 0;
    while (i < 256) {
        words[i] = inw(ata_data);
        i = i + 1;
    }
    return true;
}

// Writes exactly one 512-byte sector from buffer, then flushes the
// drive's write cache and waits for that to finish - without this, a
// read of the same sector immediately after could plausibly still see
// stale data depending on the (emulated) drive's caching behavior, and
// a real drive's cache genuinely isn't guaranteed durable until flushed.
bool ata_write_sector(u32 lba, u8* buffer) {
    if (!ata_setup(lba, 1)) {
        return false;
    }
    outb(ata_command, ata_cmd_write);
    if (!ata_wait_drq()) {
        return false;
    }
    u16* words = (u16*) buffer;
    int i = 0;
    while (i < 256) {
        outw(ata_data, words[i]);
        i = i + 1;
    }
    outb(ata_command, ata_cmd_flush);
    return ata_wait_ready();
}
