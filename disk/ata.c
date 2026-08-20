// A legacy ATA (IDE) PIO driver - the first real storage I/O this kernel
// does. Same hand-rolled, direct-port-I/O style as every earlier driver
// (VGA/keyboard/PIT): no libc, no BIOS calls, just the raw registers a
// real IDE controller exposes at the classic ISA port addresses every
// PC-compatible (including QEMU's default machine type) still wires up
// for backward compatibility.
//
// Deliberately polling, not interrupt-driven - one fewer moving part
// while proving the basic read/write path works.

#include "ata.h"
#include "../drivers/io.h"

static const u16 ATA_DATA = 0x1F0;
static const u16 ATA_SECTOR_COUNT = 0x1F2;
static const u16 ATA_LBA_LOW = 0x1F3;
static const u16 ATA_LBA_MID = 0x1F4;
static const u16 ATA_LBA_HIGH = 0x1F5;
static const u16 ATA_DRIVE_HEAD = 0x1F6;
static const u16 ATA_STATUS = 0x1F7;
static const u16 ATA_COMMAND = 0x1F7;

static const u8 ATA_STATUS_ERR = 0x01;
static const u8 ATA_STATUS_DRQ = 0x08;
static const u8 ATA_STATUS_BSY = 0x80;

static const u8 ATA_CMD_READ = 0x20;
static const u8 ATA_CMD_WRITE = 0x30;
static const u8 ATA_CMD_FLUSH = 0xE7;

// Bounded, not an unconditional `while (busy) {}` - a real IDE
// controller answers in microseconds, so hitting this cap means
// something is genuinely wrong (no disk attached, wrong port, a bad
// QEMU `-drive` flag) and the caller should get a clean failure back
// instead of the kernel hanging forever waiting on hardware that isn't
// there.
bool ata_wait_ready(void) {
    u32 spins = 0;
    while (spins < 1000000) {
        if ((inb(ATA_STATUS) & ATA_STATUS_BSY) == 0) {
            return true;
        }
        spins = spins + 1;
    }
    return false;
}

bool ata_wait_drq(void) {
    u32 spins = 0;
    while (spins < 1000000) {
        u8 status = inb(ATA_STATUS);
        if ((status & ATA_STATUS_ERR) != 0) {
            return false;
        }
        if ((status & ATA_STATUS_BSY) == 0 && (status & ATA_STATUS_DRQ) != 0) {
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
static bool ata_setup(u32 lba, u8 sector_count) {
    if (!ata_wait_ready()) {
        return false;
    }
    // 0xE0: LBA mode + drive 0 (master), ORed with LBA bits 24-27.
    outb(ATA_DRIVE_HEAD, (u8) (0xE0 | ((lba >> 24) & 0x0F)));
    outb(ATA_SECTOR_COUNT, sector_count);
    outb(ATA_LBA_LOW, (u8) (lba & 0xFF));
    outb(ATA_LBA_MID, (u8) ((lba >> 8) & 0xFF));
    outb(ATA_LBA_HIGH, (u8) ((lba >> 16) & 0xFF));
    return true;
}

// Reads exactly one 512-byte sector into buffer (must have room for
// 512 bytes). Transfers 256 16-bit words, not 512 bytes one at a time -
// the data port is genuinely 16 bits wide on real ATA hardware.
bool ata_read_sector(u32 lba, u8* buffer) {
    if (!ata_setup(lba, 1)) {
        return false;
    }
    outb(ATA_COMMAND, ATA_CMD_READ);
    if (!ata_wait_drq()) {
        return false;
    }
    u16* words = (u16*) buffer;
    int i = 0;
    while (i < 256) {
        words[i] = inw(ATA_DATA);
        i = i + 1;
    }
    return true;
}

// Writes exactly one 512-byte sector from buffer, then flushes the
// drive's write cache and waits for that to finish - without this, a
// read of the same sector immediately after could plausibly still see
// stale data depending on the (emulated) drive's caching behavior.
bool ata_write_sector(u32 lba, u8* buffer) {
    if (!ata_setup(lba, 1)) {
        return false;
    }
    outb(ATA_COMMAND, ATA_CMD_WRITE);
    if (!ata_wait_drq()) {
        return false;
    }
    u16* words = (u16*) buffer;
    int i = 0;
    while (i < 256) {
        outw(ATA_DATA, words[i]);
        i = i + 1;
    }
    outb(ATA_COMMAND, ATA_CMD_FLUSH);
    return ata_wait_ready();
}
