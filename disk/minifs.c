// MiniFS: flat namespace, fixed-size directory, on-disk from LBA 500
// (clear of the `disk`/`diskwrite` test fixtures at LBA 1/100).
//   LBA 500 superblock, LBA 501 directory (16 x 32-byte entries), LBA 502+ data.

#include "minifs.h"
#include "ata.h"
#include "../lib/strings.h"

static const u32 SUPERBLOCK_LBA = 500;
static const u32 DIRECTORY_LBA = 501;
static const u32 DATA_START_LBA = 502;
static const u32 MAX_FILES = 16;
static const u32 MINIFS_MAGIC = 0x3153464D;  // "MFS1"

typedef struct {
    u32 magic;
    u32 file_count;
} superblock;

// Padded to 32 bytes so 16 entries exactly fill one 512-byte sector.
typedef struct {
    char name[20];
    u32 start_lba;
    u32 size_bytes;
    bool used;
} dir_entry;

static u32 sectors_for(u32 bytes) {
    return (bytes + 511) / 512;
}

void copy_name(char* dst, const char* src) {
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

// Writes superblock + zeroed directory; old data region is left in place but unreachable.
bool mkfs(void) {
    u8 sb_buf[512];
    int i = 0;
    while (i < 512) {
        sb_buf[i] = 0;
        i = i + 1;
    }
    superblock* sb = (superblock*) &sb_buf[0];
    sb->magic = MINIFS_MAGIC;
    sb->file_count = 0;
    if (!ata_write_sector(SUPERBLOCK_LBA, sb_buf)) {
        return false;
    }

    u8 dir_buf[512];
    i = 0;
    while (i < 512) {
        dir_buf[i] = 0;
        i = i + 1;
    }
    return ata_write_sector(DIRECTORY_LBA, dir_buf);
}

static int find_entry(dir_entry* entries, const char* name) {
    int i = 0;
    while (i < (int) MAX_FILES) {
        if (entries[i].used && streq(entries[i].name, name)) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

// No create/open/write/close split; fails if the name already exists.
bool fs_write_file(const char* name, u8* data, u32 len) {
    u8 dir_buf[512];
    if (!ata_read_sector(DIRECTORY_LBA, dir_buf)) {
        return false;
    }
    dir_entry* entries = (dir_entry*) &dir_buf[0];

    if (find_entry(entries, name) >= 0) {
        return false;
    }

    int free_slot = -1;
    u32 next_lba = DATA_START_LBA;
    int i = 0;
    while (i < (int) MAX_FILES) {
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
    if (!ata_write_sector(DIRECTORY_LBA, dir_buf)) {
        return false;
    }

    u8 sb_buf[512];
    if (!ata_read_sector(SUPERBLOCK_LBA, sb_buf)) {
        return false;
    }
    superblock* sb = (superblock*) &sb_buf[0];
    sb->file_count = sb->file_count + 1;
    return ata_write_sector(SUPERBLOCK_LBA, sb_buf);
}

bool fs_superblock_info(u32* file_count_out) {
    u8 sb_buf[512];
    if (!ata_read_sector(SUPERBLOCK_LBA, sb_buf)) {
        return false;
    }
    superblock* sb = (superblock*) &sb_buf[0];
    *file_count_out = sb->file_count;
    return true;
}

bool fs_list_entry(int index, char* name_out, u32* size_out) {
    u8 dir_buf[512];
    if (!ata_read_sector(DIRECTORY_LBA, dir_buf)) {
        return false;
    }
    dir_entry* entries = (dir_entry*) &dir_buf[0];
    if (index < 0 || index >= (int) MAX_FILES || !entries[index].used) {
        return false;
    }
    copy_name(name_out, entries[index].name);
    *size_out = entries[index].size_bytes;
    return true;
}

// Returns byte count read, -1 if not found, -2 if too big for max_len.
int fs_read_file(const char* name, u8* out_buffer, u32 max_len) {
    u8 dir_buf[512];
    if (!ata_read_sector(DIRECTORY_LBA, dir_buf)) {
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
