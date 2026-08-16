// Milestone 17: a minimal custom filesystem ("MiniFS") on top of
// milestone 16's ATA PIO driver - deliberately simpler than FAT32/ext2
// for this first pass (those become later VFS backends once milestone
// 18's VFS layer exists above this, the whole point of having one).
// Flat namespace (no directories), fixed-size directory, contiguous
// per-file allocation recomputed from the directory each time rather
// than a persistent free list - the same "prove the mechanism works
// before optimizing it" reasoning that put a bump allocator before the
// heap's free list, and a static identity map before dynamic paging.
//
// On-disk layout, entirely within a reserved region starting at LBA
// 500 - chosen specifically clear of milestone 16's own test fixtures
// (the `disk` command's signature at LBA 1, `diskwrite`'s scratch
// sector at LBA 100), so neither milestone's verification regresses:
//
//   LBA 500          Superblock (magic + file count, rest unused)
//   LBA 501          Directory - 16 fixed 32-byte DirEntry slots,
//                    exactly filling one 512-byte sector
//   LBA 502+         Data region - each file's bytes, sector-aligned,
//                    laid out back to back in creation order

import "ata.mc";
import "../lib/strings.mc";

u32 SUPERBLOCK_LBA = 500;
u32 DIRECTORY_LBA = 501;
u32 DATA_START_LBA = 502;
u32 MAX_FILES = 16;
u32 MINIFS_MAGIC = 0x3153464D;   // ascii "MFS1", little-endian in the sector

struct Superblock {
    u32 magic;
    u32 fileCount;
}

// 20 + 4 + 4 + 1 = 29 bytes, rounded up to the struct's own 4-byte
// alignment (from the u32 fields) = 32 bytes - exactly divides the
// 512-byte directory sector into 16 slots, matching MAX_FILES.
struct DirEntry {
    char name[20];
    u32 startLba;
    u32 sizeBytes;
    bool used;
}

u32 sectorsFor(u32 bytes) {
    return (bytes + 511) / 512;
}

void copyName(char* dst, char* src) {
    int i = 0;
    while (i < 19 && src[i] != '\0') {
        dst[i] = src[i];
        i = i + 1;
    }
    while (i < 20) {
        dst[i] = '\0';
        i = i + 1;
    }
}

// Formats a fresh filesystem: a superblock carrying the magic number
// (mostly so a future fsck/mount step has something to sanity-check -
// nothing reads it back yet) and an all-zero (all-unused) directory.
// Anything previously sitting in the data region becomes unreachable,
// not explicitly wiped - harmless since nothing can name it anymore.
bool mkfs() {
    u8 sbBuf[512];
    int i = 0;
    while (i < 512) {
        sbBuf[i] = 0;
        i = i + 1;
    }
    Superblock* sb = (Superblock*) &sbBuf[0];
    sb->magic = MINIFS_MAGIC;
    sb->fileCount = 0;
    if (!ataWriteSector(SUPERBLOCK_LBA, sbBuf)) {
        return false;
    }

    u8 dirBuf[512];
    i = 0;
    while (i < 512) {
        dirBuf[i] = 0;
        i = i + 1;
    }
    return ataWriteSector(DIRECTORY_LBA, dirBuf);
}

int findEntry(DirEntry* entries, char* name) {
    int i = 0;
    while (i < (int) MAX_FILES) {
        if (entries[i].used && streq(entries[i].name, name)) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

// Creates a new file and writes its full contents in one call - no
// separate create/open/write/close steps, no partial writes, matching
// this milestone's "prove contiguous storage + a real directory work at
// all" scope. Fails outright (rather than overwriting) if the name
// already exists - truncate/append/overwrite semantics are real,
// separate work deliberately left to whenever this gets a proper file-
// handle API (milestone 18+'s VFS, or the native API further out).
bool fsWriteFile(char* name, u8* data, u32 len) {
    u8 dirBuf[512];
    if (!ataReadSector(DIRECTORY_LBA, dirBuf)) {
        return false;
    }
    DirEntry* entries = (DirEntry*) &dirBuf[0];

    if (findEntry(entries, name) >= 0) {
        return false;
    }

    int freeSlot = -1;
    u32 nextLba = DATA_START_LBA;
    int i = 0;
    while (i < (int) MAX_FILES) {
        if (entries[i].used) {
            u32 endLba = entries[i].startLba + sectorsFor(entries[i].sizeBytes);
            if (endLba > nextLba) {
                nextLba = endLba;
            }
        } else if (freeSlot < 0) {
            freeSlot = i;
        }
        i = i + 1;
    }
    if (freeSlot < 0) {
        return false;
    }

    u32 sectorCount = sectorsFor(len);
    u8 sectorBuf[512];
    u32 written = 0;
    u32 s = 0;
    while (s < sectorCount) {
        int b = 0;
        while (b < 512) {
            u32 offset = written + (u32) b;
            if (offset < len) {
                sectorBuf[b] = data[offset];
            } else {
                sectorBuf[b] = 0;
            }
            b = b + 1;
        }
        if (!ataWriteSector(nextLba + s, sectorBuf)) {
            return false;
        }
        written = written + 512;
        s = s + 1;
    }

    copyName(entries[freeSlot].name, name);
    entries[freeSlot].startLba = nextLba;
    entries[freeSlot].sizeBytes = len;
    entries[freeSlot].used = true;
    if (!ataWriteSector(DIRECTORY_LBA, dirBuf)) {
        return false;
    }

    // Keep the superblock's fileCount honest - nothing reads it back yet
    // (findEntry scans `used` flags directly), but a stale "0 files"
    // sitting next to a real directory full of entries is exactly the
    // kind of misleading on-disk state a future fsck/mount step would
    // trip over, so it's maintained correctly from day one rather than
    // left as a known gap.
    u8 sbBuf[512];
    if (!ataReadSector(SUPERBLOCK_LBA, sbBuf)) {
        return false;
    }
    Superblock* sb = (Superblock*) &sbBuf[0];
    sb->fileCount = sb->fileCount + 1;
    return ataWriteSector(SUPERBLOCK_LBA, sbBuf);
}

// Reads a file's full contents into outBuffer (caller-owned, must hold
// at least the file's real size). Returns the byte count read, or -1 if
// the file doesn't exist or is too big for maxLen.
int fsReadFile(char* name, u8* outBuffer, u32 maxLen) {
    u8 dirBuf[512];
    if (!ataReadSector(DIRECTORY_LBA, dirBuf)) {
        return -1;
    }
    DirEntry* entries = (DirEntry*) &dirBuf[0];
    int slot = findEntry(entries, name);
    if (slot < 0) {
        return -1;
    }
    u32 size = entries[slot].sizeBytes;
    if (size > maxLen) {
        return -1;
    }

    u32 sectorCount = sectorsFor(size);
    u8 sectorBuf[512];
    u32 read = 0;
    u32 s = 0;
    while (s < sectorCount) {
        if (!ataReadSector(entries[slot].startLba + s, sectorBuf)) {
            return -1;
        }
        int b = 0;
        while (b < 512) {
            u32 offset = read + (u32) b;
            if (offset < size) {
                outBuffer[offset] = sectorBuf[b];
            }
            b = b + 1;
        }
        read = read + 512;
        s = s + 1;
    }
    return (int) size;
}
