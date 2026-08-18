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

u32 superblock_lba = 500;
u32 directory_lba = 501;
u32 data_start_lba = 502;
u32 max_files = 16;
u32 minifs_magic = 0x3153464D;   // ascii "MFS1", little-endian in the sector

struct superblock {
    u32 magic;
    u32 file_count;
}

// 20 + 4 + 4 + 1 = 29 bytes, rounded up to the struct's own 4-byte
// alignment (from the u32 fields) = 32 bytes - exactly divides the
// 512-byte directory sector into 16 slots, matching MAX_FILES.
struct dir_entry {
    char name[20];
    u32 start_lba;
    u32 size_bytes;
    bool used;
}

u32 sectors_for(u32 bytes) {
    return (bytes + 511) / 512;
}

void copy_name(char* dst, char* src) {
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
    u8 sb_buf[512];
    int i = 0;
    while (i < 512) {
        sb_buf[i] = 0;
        i = i + 1;
    }
    superblock* sb = (superblock*) &sb_buf[0];
    sb->magic = minifs_magic;
    sb->file_count = 0;
    if (!ata_write_sector(superblock_lba, sb_buf)) {
        return false;
    }

    u8 dir_buf[512];
    i = 0;
    while (i < 512) {
        dir_buf[i] = 0;
        i = i + 1;
    }
    return ata_write_sector(directory_lba, dir_buf);
}

int find_entry(dir_entry* entries, char* name) {
    int i = 0;
    while (i < (int) max_files) {
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
bool fs_write_file(char* name, u8* data, u32 len) {
    u8 dir_buf[512];
    if (!ata_read_sector(directory_lba, dir_buf)) {
        return false;
    }
    dir_entry* entries = (dir_entry*) &dir_buf[0];

    if (find_entry(entries, name) >= 0) {
        return false;
    }

    int free_slot = -1;
    u32 next_lba = data_start_lba;
    int i = 0;
    while (i < (int) max_files) {
        if (entries[i].used) {
            u32 end_lba = entries[i].start_lba + sectors_for(entries[i].size_bytes);
            if (end_lba > next_lba) {
                next_lba = end_lba;
            }
        } else if (free_slot < 0) {
            free_slot = i;
        }
        i = i + 1;
    }
    if (free_slot < 0) {
        return false;
    }

    u32 sector_count = sectors_for(len);
    u8 sector_buf[512];
    u32 written = 0;
    u32 s = 0;
    while (s < sector_count) {
        int b = 0;
        while (b < 512) {
            u32 offset = written + (u32) b;
            if (offset < len) {
                sector_buf[b] = data[offset];
            } else {
                sector_buf[b] = 0;
            }
            b = b + 1;
        }
        if (!ata_write_sector(next_lba + s, sector_buf)) {
            return false;
        }
        written = written + 512;
        s = s + 1;
    }

    copy_name(entries[free_slot].name, name);
    entries[free_slot].start_lba = next_lba;
    entries[free_slot].size_bytes = len;
    entries[free_slot].used = true;
    if (!ata_write_sector(directory_lba, dir_buf)) {
        return false;
    }

    // Keep the superblock's file_count honest - nothing reads it back yet
    // (find_entry scans `used` flags directly), but a stale "0 files"
    // sitting next to a real directory full of entries is exactly the
    // kind of misleading on-disk state a future fsck/mount step would
    // trip over, so it's maintained correctly from day one rather than
    // left as a known gap.
    u8 sb_buf[512];
    if (!ata_read_sector(superblock_lba, sb_buf)) {
        return false;
    }
    superblock* sb = (superblock*) &sb_buf[0];
    sb->file_count = sb->file_count + 1;
    return ata_write_sector(superblock_lba, sb_buf);
}

// Reads a file's full contents into out_buffer (caller-owned, must hold
// at least the file's real size). Returns the byte count read, -1 if
// the file doesn't exist, or -2 if it exists but is too big for max_len -
// two genuinely different failures a caller might want to react to
// differently (milestone 19's `vfscat` was reporting a real 208-byte
// file as "not found" against its 128-byte display buffer before this
// distinction existed, a real ambiguity caught by testing, not a
// hypothetical).
int fs_read_file(char* name, u8* out_buffer, u32 max_len) {
    u8 dir_buf[512];
    if (!ata_read_sector(directory_lba, dir_buf)) {
        return -1;
    }
    dir_entry* entries = (dir_entry*) &dir_buf[0];
    int slot = find_entry(entries, name);
    if (slot < 0) {
        return -1;
    }
    u32 size = entries[slot].size_bytes;
    if (size > max_len) {
        return -2;
    }

    u32 sector_count = sectors_for(size);
    u8 sector_buf[512];
    u32 read = 0;
    u32 s = 0;
    while (s < sector_count) {
        if (!ata_read_sector(entries[slot].start_lba + s, sector_buf)) {
            return -1;
        }
        int b = 0;
        while (b < 512) {
            u32 offset = read + (u32) b;
            if (offset < size) {
                out_buffer[offset] = sector_buf[b];
            }
            b = b + 1;
        }
        read = read + 512;
        s = s + 1;
    }
    return (int) size;
}
