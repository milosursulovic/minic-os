// MiniFS: a real hierarchical filesystem, not a flat one - each directory
// (root or any subdirectory) is still exactly one fixed 512-byte sector
// of 16 dir_entry slots, byte-for-byte the same shape this file has
// always used; an entry just now can point to ANOTHER directory sector
// instead of only file data (is_dir), so the 16-per-sector cap is a
// per-directory-level limit, not a whole-filesystem one - the tree as a
// whole is bounded only by real disk space (next_free_lba, a simple bump
// allocator that never reclaims deleted space - same deliberate
// simplicity every allocator in this kernel started with).
//   LBA 500 superblock, LBA 501 root directory, LBA 502+ bump-allocated
//   (both file data AND subdirectory sectors come from the same pool).

#include "minifs.h"
#include "../ata/ata.h"
#include "../../lib/strings.h"

static const u32 SUPERBLOCK_LBA = 500;
static const u32 ROOT_LBA = 501;
static const u32 DATA_START_LBA = 502;
static const u32 MAX_FILES = 16;
static const u32 MAX_PATH_DEPTH = 8;
static const u32 MINIFS_MAGIC = 0x3153464D;  // "MFS1"

typedef struct {
    u32 magic;
    u32 file_count;
    u32 next_free_lba;
} superblock;

// Padded to 32 bytes so 16 entries exactly fill one 512-byte sector -
// is_dir fits in padding that was already there (name+start_lba+
// size_bytes+used = 29 bytes, rounds up to 32 regardless).
typedef struct {
    char name[20];
    u32 start_lba;    // file: first data sector. directory: its own directory sector.
    u32 size_bytes;    // file: byte length. directory: unused (0).
    bool used;
    bool is_dir;
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

// Writes superblock + zeroed root directory; old data/subdirectory
// sectors are left in place but unreachable.
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
    sb->next_free_lba = DATA_START_LBA;
    if (!ata_write_sector(SUPERBLOCK_LBA, sb_buf)) {
        return false;
    }

    u8 dir_buf[512];
    i = 0;
    while (i < 512) {
        dir_buf[i] = 0;
        i = i + 1;
    }
    return ata_write_sector(ROOT_LBA, dir_buf);
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

// Splits "photos/vacation/beach.jpg" into up to MAX_PATH_DEPTH components
// (no '/' left in any component). No shared string-split helper exists
// in lib/strings.c to reuse - plain char-by-char scan, same style as the
// rest of this file.
static int split_path(const char* path, char components[][20]) {
    int count = 0;
    while (count < (int) MAX_PATH_DEPTH && *path != '\0') {
        int i = 0;
        while (*path != '\0' && *path != '/' && i < 19) {
            components[count][i] = *path;
            i = i + 1;
            path = path + 1;
        }
        components[count][i] = '\0';
        while (*path == '/') {
            path = path + 1;
        }
        count = count + 1;
    }
    return count;
}

// Walks every component except the last (each must already exist and be
// a directory), returns the LBA of the directory that should hold the
// final component. A bare name with no '/' (every existing flat caller -
// "file0.mfs", "testprog.bin", etc.) resolves straight to the root
// directory - fully backward compatible.
static bool resolve_parent_dir(const char* path, u32* parent_lba_out, char* last_component_out) {
    char components[MAX_PATH_DEPTH][20];
    int count = split_path(path, components);
    if (count == 0) {
        return false;
    }
    u32 current_lba = ROOT_LBA;
    int i = 0;
    while (i < count - 1) {
        u8 dir_buf[512];
        if (!ata_read_sector(current_lba, dir_buf)) {
            return false;
        }
        dir_entry* entries = (dir_entry*) &dir_buf[0];
        int slot = find_entry(entries, components[i]);
        if (slot < 0 || !entries[slot].is_dir) {
            return false;
        }
        current_lba = entries[slot].start_lba;
        i = i + 1;
    }
    *parent_lba_out = current_lba;
    copy_name(last_component_out, components[count - 1]);
    return true;
}

// Walks EVERY component (including the last) as a directory - used for
// listing, since fs_list_entry lists an arbitrary directory's own
// contents, not just root's. Empty path resolves to root.
static bool resolve_dir(const char* path, u32* dir_lba_out) {
    char components[MAX_PATH_DEPTH][20];
    int count = split_path(path, components);
    u32 current_lba = ROOT_LBA;
    int i = 0;
    while (i < count) {
        u8 dir_buf[512];
        if (!ata_read_sector(current_lba, dir_buf)) {
            return false;
        }
        dir_entry* entries = (dir_entry*) &dir_buf[0];
        int slot = find_entry(entries, components[i]);
        if (slot < 0 || !entries[slot].is_dir) {
            return false;
        }
        current_lba = entries[slot].start_lba;
        i = i + 1;
    }
    *dir_lba_out = current_lba;
    return true;
}

static bool alloc_lba(u32 sector_count, u32* lba_out) {
    u8 sb_buf[512];
    if (!ata_read_sector(SUPERBLOCK_LBA, sb_buf)) {
        return false;
    }
    superblock* sb = (superblock*) &sb_buf[0];
    *lba_out = sb->next_free_lba;
    sb->next_free_lba = sb->next_free_lba + sector_count;
    return ata_write_sector(SUPERBLOCK_LBA, sb_buf);
}

static bool bump_file_count(int delta) {
    u8 sb_buf[512];
    if (!ata_read_sector(SUPERBLOCK_LBA, sb_buf)) {
        return false;
    }
    superblock* sb = (superblock*) &sb_buf[0];
    sb->file_count = (u32) ((int) sb->file_count + delta);
    return ata_write_sector(SUPERBLOCK_LBA, sb_buf);
}

// No create/open/write/close split; fails if the name already exists.
bool fs_write_file(const char* path, u8* data, u32 len) {
    u32 parent_lba;
    char name[20];
    if (!resolve_parent_dir(path, &parent_lba, name)) {
        return false;
    }
    u8 dir_buf[512];
    if (!ata_read_sector(parent_lba, dir_buf)) {
        return false;
    }
    dir_entry* entries = (dir_entry*) &dir_buf[0];

    if (find_entry(entries, name) >= 0) {
        return false;
    }
    int free_slot = -1;
    int i = 0;
    while (i < (int) MAX_FILES) {
        if (!entries[i].used && free_slot < 0) {
            free_slot = i;
        }
        i = i + 1;
    }
    if (free_slot < 0) {
        return false;
    }

    u32 sector_count = sectors_for(len);
    u32 start_lba;
    if (!alloc_lba(sector_count, &start_lba)) {
        return false;
    }

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
        if (!ata_write_sector(start_lba + s, sector_buf)) {
            return false;
        }
        written = written + 512;
        s = s + 1;
    }

    copy_name(entries[free_slot].name, name);
    entries[free_slot].start_lba = start_lba;
    entries[free_slot].size_bytes = len;
    entries[free_slot].used = true;
    entries[free_slot].is_dir = false;
    if (!ata_write_sector(parent_lba, dir_buf)) {
        return false;
    }
    return bump_file_count(1);
}

bool fs_create_dir(const char* path) {
    u32 parent_lba;
    char name[20];
    if (!resolve_parent_dir(path, &parent_lba, name)) {
        return false;
    }
    u8 dir_buf[512];
    if (!ata_read_sector(parent_lba, dir_buf)) {
        return false;
    }
    dir_entry* entries = (dir_entry*) &dir_buf[0];

    if (find_entry(entries, name) >= 0) {
        return false;
    }
    int free_slot = -1;
    int i = 0;
    while (i < (int) MAX_FILES) {
        if (!entries[i].used && free_slot < 0) {
            free_slot = i;
        }
        i = i + 1;
    }
    if (free_slot < 0) {
        return false;
    }

    u32 new_dir_lba;
    if (!alloc_lba(1, &new_dir_lba)) {
        return false;
    }
    u8 zero_buf[512];
    i = 0;
    while (i < 512) {
        zero_buf[i] = 0;
        i = i + 1;
    }
    if (!ata_write_sector(new_dir_lba, zero_buf)) {
        return false;
    }

    copy_name(entries[free_slot].name, name);
    entries[free_slot].start_lba = new_dir_lba;
    entries[free_slot].size_bytes = 0;
    entries[free_slot].used = true;
    entries[free_slot].is_dir = true;
    if (!ata_write_sector(parent_lba, dir_buf)) {
        return false;
    }
    return bump_file_count(1);
}

bool fs_delete_file(const char* path) {
    u32 parent_lba;
    char name[20];
    if (!resolve_parent_dir(path, &parent_lba, name)) {
        return false;
    }
    u8 dir_buf[512];
    if (!ata_read_sector(parent_lba, dir_buf)) {
        return false;
    }
    dir_entry* entries = (dir_entry*) &dir_buf[0];
    int slot = find_entry(entries, name);
    if (slot < 0) {
        return false;
    }
    entries[slot].used = false;
    if (!ata_write_sector(parent_lba, dir_buf)) {
        return false;
    }
    return bump_file_count(-1);
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

bool fs_list_entry(const char* dir_path, int index, char* name_out, u32* size_out, bool* is_dir_out) {
    u32 dir_lba;
    if (!resolve_dir(dir_path, &dir_lba)) {
        return false;
    }
    u8 dir_buf[512];
    if (!ata_read_sector(dir_lba, dir_buf)) {
        return false;
    }
    dir_entry* entries = (dir_entry*) &dir_buf[0];
    if (index < 0 || index >= (int) MAX_FILES || !entries[index].used) {
        return false;
    }
    copy_name(name_out, entries[index].name);
    *size_out = entries[index].size_bytes;
    *is_dir_out = entries[index].is_dir;
    return true;
}

bool fs_dir_exists(const char* path) {
    u32 dir_lba;
    return resolve_dir(path, &dir_lba);
}

// Returns byte count read, -1 if not found (or path resolves to a
// directory - reading a directory as a file fails the same way), -2 if
// too big for max_len.
int fs_read_file(const char* path, u8* out_buffer, u32 max_len) {
    u32 parent_lba;
    char name[20];
    if (!resolve_parent_dir(path, &parent_lba, name)) {
        return -1;
    }
    u8 dir_buf[512];
    if (!ata_read_sector(parent_lba, dir_buf)) {
        return -1;
    }
    dir_entry* entries = (dir_entry*) &dir_buf[0];
    int slot = find_entry(entries, name);
    if (slot < 0 || entries[slot].is_dir) {
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
