// Real, hand-written FAT32 driver - see fat32.h for the design story and
// documented scope limits.

#include "fat32.h"
#include "../ata/ata.h"

#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20
#define ATTR_LONG_NAME 0x0F

#define FAT32_EOC 0x0FFFFFFF
#define FAT32_FREE 0x00000000
#define FAT_ENTRY_MASK 0x0FFFFFFF

#define MAX_PATH_DEPTH 8

typedef struct __attribute__((packed)) {
    char name[8];
    char ext[3];
    u8 attr;
    u8 reserved1;
    u8 create_time_tenth;
    u16 create_time;
    u16 create_date;
    u16 access_date;
    u16 first_cluster_high;
    u16 write_time;
    u16 write_date;
    u16 first_cluster_low;
    u32 file_size;
} fat32_dir_entry;

typedef struct {
    bool initialized;
    u8 drive;
    u16 bytes_per_sector;
    u8 sectors_per_cluster;
    u16 reserved_sector_count;
    u8 num_fats;
    u32 fat_size;
    u32 root_cluster;
    u32 first_fat_lba;
    u32 first_data_lba;
    u32 total_clusters;
} fat32_state;

static fat32_state g_fat32;

static u16 read_u16(const u8* p) {
    return (u16) (p[0] | ((u16) p[1] << 8));
}

static u32 read_u32(const u8* p) {
    return (u32) p[0] | ((u32) p[1] << 8) | ((u32) p[2] << 16) | ((u32) p[3] << 24);
}

static void write_u32(u8* p, u32 value) {
    p[0] = (u8) (value & 0xFF);
    p[1] = (u8) ((value >> 8) & 0xFF);
    p[2] = (u8) ((value >> 16) & 0xFF);
    p[3] = (u8) ((value >> 24) & 0xFF);
}

bool fat32_init(u8 drive) {
    u8 sector[512];
    if (!ata_read_sector_drive(drive, 0, sector)) {
        return false;
    }
    if (sector[510] != 0x55 || sector[511] != 0xAA) {
        return false;
    }
    g_fat32.drive = drive;
    g_fat32.bytes_per_sector = read_u16(&sector[11]);
    g_fat32.sectors_per_cluster = sector[13];
    g_fat32.reserved_sector_count = read_u16(&sector[14]);
    g_fat32.num_fats = sector[16];
    g_fat32.fat_size = read_u32(&sector[36]);
    g_fat32.root_cluster = read_u32(&sector[44]);
    g_fat32.first_fat_lba = g_fat32.reserved_sector_count;
    g_fat32.first_data_lba = g_fat32.reserved_sector_count + ((u32) g_fat32.num_fats * g_fat32.fat_size);
    u32 total_sectors_32 = read_u32(&sector[32]);
    u32 data_sectors = total_sectors_32 - g_fat32.first_data_lba;
    g_fat32.total_clusters = data_sectors / g_fat32.sectors_per_cluster;
    if (g_fat32.bytes_per_sector != 512 || g_fat32.sectors_per_cluster == 0
        || g_fat32.fat_size == 0 || g_fat32.root_cluster < 2) {
        return false;
    }
    g_fat32.initialized = true;
    return true;
}

static u32 cluster_to_lba(u32 cluster) {
    return g_fat32.first_data_lba + (cluster - 2) * (u32) g_fat32.sectors_per_cluster;
}

static u32 get_fat_entry(u32 cluster) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector = g_fat32.first_fat_lba + (fat_offset / g_fat32.bytes_per_sector);
    u32 offset_in_sector = fat_offset % g_fat32.bytes_per_sector;
    u8 sector[512];
    if (!ata_read_sector_drive(g_fat32.drive, fat_sector, sector)) {
        return FAT32_EOC;
    }
    return read_u32(&sector[offset_in_sector]) & FAT_ENTRY_MASK;
}

static void set_fat_entry(u32 cluster, u32 value) {
    u32 fat_offset = cluster * 4;
    u32 fat_sector = g_fat32.first_fat_lba + (fat_offset / g_fat32.bytes_per_sector);
    u32 offset_in_sector = fat_offset % g_fat32.bytes_per_sector;
    u8 sector[512];
    if (!ata_read_sector_drive(g_fat32.drive, fat_sector, sector)) {
        return;
    }
    u32 preserved_top = read_u32(&sector[offset_in_sector]) & ~FAT_ENTRY_MASK;
    write_u32(&sector[offset_in_sector], (value & FAT_ENTRY_MASK) | preserved_top);
    ata_write_sector_drive(g_fat32.drive, fat_sector, sector);
}

static bool cluster_in_use(u32 entry) {
    return entry != FAT32_FREE;
}

// Real O(n) scan from cluster 2 - no FSInfo-sector caching (see fat32.h).
// Marks the found cluster EOC immediately to claim it. Returns 0 on
// failure (0/1 are never valid data cluster numbers).
static u32 alloc_cluster(void) {
    u32 cluster = 2;
    while (cluster < g_fat32.total_clusters + 2) {
        if (!cluster_in_use(get_fat_entry(cluster))) {
            set_fat_entry(cluster, FAT32_EOC);
            u8 zero[512];
            int i = 0;
            while (i < 512) {
                zero[i] = 0;
                i = i + 1;
            }
            int s = 0;
            while (s < g_fat32.sectors_per_cluster) {
                ata_write_sector_drive(g_fat32.drive, cluster_to_lba(cluster) + (u32) s, zero);
                s = s + 1;
            }
            return cluster;
        }
        cluster = cluster + 1;
    }
    return 0;
}

static void free_cluster_chain(u32 start_cluster) {
    u32 cluster = start_cluster;
    while (cluster >= 2 && cluster < FAT32_EOC - 8) {
        u32 next = get_fat_entry(cluster);
        set_fat_entry(cluster, FAT32_FREE);
        if (next < 2 || next >= FAT32_EOC - 8) {
            break;
        }
        cluster = next;
    }
}

static void name_to_83(const char* input, char out[11]) {
    int i = 0;
    while (i < 11) {
        out[i] = ' ';
        i = i + 1;
    }
    int in_i = 0;
    int out_i = 0;
    while (input[in_i] != '\0' && input[in_i] != '.' && out_i < 8) {
        char c = input[in_i];
        if (c >= 'a' && c <= 'z') {
            c = (char) (c - 'a' + 'A');
        }
        out[out_i] = c;
        out_i = out_i + 1;
        in_i = in_i + 1;
    }
    while (input[in_i] != '\0' && input[in_i] != '.') {
        in_i = in_i + 1;
    }
    if (input[in_i] == '.') {
        in_i = in_i + 1;
        int ext_i = 8;
        while (input[in_i] != '\0' && ext_i < 11) {
            char c = input[in_i];
            if (c >= 'a' && c <= 'z') {
                c = (char) (c - 'a' + 'A');
            }
            out[ext_i] = c;
            ext_i = ext_i + 1;
            in_i = in_i + 1;
        }
    }
}

static bool name_matches_83(const fat32_dir_entry* e, const char name_83[11]) {
    int i = 0;
    while (i < 11) {
        if (e->name[i] != name_83[i]) {
            return false;
        }
        i = i + 1;
    }
    return true;
}

static void entry_to_display_name(const fat32_dir_entry* e, char* out) {
    int out_i = 0;
    int i = 0;
    while (i < 8 && e->name[i] != ' ') {
        out[out_i] = e->name[i];
        out_i = out_i + 1;
        i = i + 1;
    }
    bool has_ext = e->ext[0] != ' ';
    if (has_ext) {
        out[out_i] = '.';
        out_i = out_i + 1;
        i = 0;
        while (i < 3 && e->ext[i] != ' ') {
            out[out_i] = e->ext[i];
            out_i = out_i + 1;
            i = i + 1;
        }
    }
    out[out_i] = '\0';
}

static u32 entry_first_cluster(const fat32_dir_entry* e) {
    return ((u32) e->first_cluster_high << 16) | (u32) e->first_cluster_low;
}

static void set_entry_first_cluster(fat32_dir_entry* e, u32 cluster) {
    e->first_cluster_high = (u16) ((cluster >> 16) & 0xFFFF);
    e->first_cluster_low = (u16) (cluster & 0xFFFF);
}

static int split_path(const char* path, char components[][12]) {
    int count = 0;
    while (count < MAX_PATH_DEPTH && *path != '\0') {
        char raw[64];
        int i = 0;
        while (*path != '\0' && *path != '/' && i < 63) {
            raw[i] = *path;
            i = i + 1;
            path = path + 1;
        }
        raw[i] = '\0';
        while (*path == '/') {
            path = path + 1;
        }
        name_to_83(raw, components[count]);
        count = count + 1;
    }
    return count;
}

// Scans one directory's whole cluster chain for name_83. On success,
// fills *entry and the exact on-disk location (*lba, *offset) so a
// caller can update it in place (delete/rewrite).
static bool find_entry_in_dir(u32 dir_cluster, const char name_83[11], fat32_dir_entry* out,
                                u32* out_lba, u32* out_offset) {
    u32 cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT32_EOC - 8) {
        u32 s = 0;
        while (s < g_fat32.sectors_per_cluster) {
            u32 lba = cluster_to_lba(cluster) + s;
            u8 sector[512];
            if (!ata_read_sector_drive(g_fat32.drive, lba, sector)) {
                return false;
            }
            u32 off = 0;
            while (off < 512) {
                fat32_dir_entry* e = (fat32_dir_entry*) &sector[off];
                if (e->name[0] == 0x00) {
                    return false;  // end of valid entries in this directory
                }
                if ((u8) e->name[0] != 0xE5 && (e->attr & ATTR_LONG_NAME) != ATTR_LONG_NAME
                    && (e->attr & ATTR_VOLUME_ID) == 0) {
                    if (name_matches_83(e, name_83)) {
                        *out = *e;
                        *out_lba = lba;
                        *out_offset = off;
                        return true;
                    }
                }
                off = off + 32;
            }
            s = s + 1;
        }
        cluster = get_fat_entry(cluster);
    }
    return false;
}

// Finds a never-used or deleted slot in a directory's chain, growing the
// chain by one cluster (real directory growth) if every existing slot
// is taken.
static bool find_free_dir_slot(u32 dir_cluster, u32* out_lba, u32* out_offset) {
    u32 cluster = dir_cluster;
    u32 last_cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT32_EOC - 8) {
        last_cluster = cluster;
        u32 s = 0;
        while (s < g_fat32.sectors_per_cluster) {
            u32 lba = cluster_to_lba(cluster) + s;
            u8 sector[512];
            if (!ata_read_sector_drive(g_fat32.drive, lba, sector)) {
                return false;
            }
            u32 off = 0;
            while (off < 512) {
                fat32_dir_entry* e = (fat32_dir_entry*) &sector[off];
                if (e->name[0] == 0x00 || (u8) e->name[0] == 0xE5) {
                    *out_lba = lba;
                    *out_offset = off;
                    return true;
                }
                off = off + 32;
            }
            s = s + 1;
        }
        cluster = get_fat_entry(cluster);
    }
    u32 new_cluster = alloc_cluster();
    if (new_cluster == 0) {
        return false;
    }
    set_fat_entry(last_cluster, new_cluster);
    *out_lba = cluster_to_lba(new_cluster);
    *out_offset = 0;
    return true;
}

static void write_entry_at(u32 lba, u32 offset, const fat32_dir_entry* entry) {
    u8 sector[512];
    if (!ata_read_sector_drive(g_fat32.drive, lba, sector)) {
        return;
    }
    fat32_dir_entry* dst = (fat32_dir_entry*) &sector[offset];
    *dst = *entry;
    ata_write_sector_drive(g_fat32.drive, lba, sector);
}

// Walks every component except the last, returns the directory cluster
// that should hold the final component plus that component's own 8.3
// name. A bare name (no '/') resolves against the root directory.
static bool resolve_parent_dir(const char* path, u32* parent_cluster_out, char last_name_out[11]) {
    char components[MAX_PATH_DEPTH][12];
    int count = split_path(path, components);
    if (count == 0) {
        return false;
    }
    u32 current = g_fat32.root_cluster;
    int i = 0;
    while (i < count - 1) {
        fat32_dir_entry e;
        u32 lba, off;
        if (!find_entry_in_dir(current, components[i], &e, &lba, &off) || (e.attr & ATTR_DIRECTORY) == 0) {
            return false;
        }
        current = entry_first_cluster(&e);
        i = i + 1;
    }
    *parent_cluster_out = current;
    int j = 0;
    while (j < 11) {
        last_name_out[j] = components[count - 1][j];
        j = j + 1;
    }
    return true;
}

// Walks every component INCLUDING the last as a directory - for listing
// an arbitrary directory's own contents. Empty path resolves to root.
static bool resolve_dir(const char* path, u32* dir_cluster_out) {
    char components[MAX_PATH_DEPTH][12];
    int count = split_path(path, components);
    u32 current = g_fat32.root_cluster;
    int i = 0;
    while (i < count) {
        fat32_dir_entry e;
        u32 lba, off;
        if (!find_entry_in_dir(current, components[i], &e, &lba, &off) || (e.attr & ATTR_DIRECTORY) == 0) {
            return false;
        }
        current = entry_first_cluster(&e);
        i = i + 1;
    }
    *dir_cluster_out = current;
    return true;
}

int fat32_read_file(const char* path, u8* out_buffer, u32 max_len) {
    if (!g_fat32.initialized) {
        return -1;
    }
    u32 parent_cluster;
    char name_83[11];
    if (!resolve_parent_dir(path, &parent_cluster, name_83)) {
        return -1;
    }
    fat32_dir_entry e;
    u32 lba, off;
    if (!find_entry_in_dir(parent_cluster, name_83, &e, &lba, &off) || (e.attr & ATTR_DIRECTORY) != 0) {
        return -1;
    }
    if (e.file_size > max_len) {
        return -2;
    }
    u32 cluster = entry_first_cluster(&e);
    u32 read_total = 0;
    while (cluster >= 2 && cluster < FAT32_EOC - 8 && read_total < e.file_size) {
        u32 s = 0;
        while (s < g_fat32.sectors_per_cluster && read_total < e.file_size) {
            u8 sector[512];
            if (!ata_read_sector_drive(g_fat32.drive, cluster_to_lba(cluster) + s, sector)) {
                return -1;
            }
            u32 b = 0;
            while (b < 512 && read_total < e.file_size) {
                out_buffer[read_total] = sector[b];
                read_total = read_total + 1;
                b = b + 1;
            }
            s = s + 1;
        }
        cluster = get_fat_entry(cluster);
    }
    return (int) read_total;
}

bool fat32_write_file(const char* path, u8* data, u32 len) {
    if (!g_fat32.initialized) {
        return false;
    }
    u32 parent_cluster;
    char name_83[11];
    if (!resolve_parent_dir(path, &parent_cluster, name_83)) {
        return false;
    }
    fat32_dir_entry existing;
    u32 existing_lba, existing_off;
    if (find_entry_in_dir(parent_cluster, name_83, &existing, &existing_lba, &existing_off)) {
        return false;  // create-only - see fat32.h's own doc comment
    }

    u32 first_cluster = 0;
    u32 prev_cluster = 0;
    u32 written = 0;
    if (len > 0) {
        while (written < len) {
            u32 c = alloc_cluster();
            if (c == 0) {
                if (first_cluster != 0) {
                    free_cluster_chain(first_cluster);
                }
                return false;
            }
            if (first_cluster == 0) {
                first_cluster = c;
            } else {
                set_fat_entry(prev_cluster, c);
            }
            prev_cluster = c;

            u32 s = 0;
            while (s < g_fat32.sectors_per_cluster && written < len) {
                u8 sector[512];
                int i = 0;
                while (i < 512) {
                    sector[i] = 0;
                    i = i + 1;
                }
                u32 b = 0;
                while (b < 512 && written < len) {
                    sector[b] = data[written];
                    written = written + 1;
                    b = b + 1;
                }
                ata_write_sector_drive(g_fat32.drive, cluster_to_lba(c) + s, sector);
                s = s + 1;
            }
        }
    }

    u32 slot_lba, slot_off;
    if (!find_free_dir_slot(parent_cluster, &slot_lba, &slot_off)) {
        if (first_cluster != 0) {
            free_cluster_chain(first_cluster);
        }
        return false;
    }
    fat32_dir_entry entry;
    int z = 0;
    u8* entry_bytes = (u8*) &entry;
    while (z < (int) sizeof(entry)) {
        entry_bytes[z] = 0;
        z = z + 1;
    }
    int ni = 0;
    while (ni < 8) {
        entry.name[ni] = name_83[ni];
        ni = ni + 1;
    }
    while (ni < 11) {
        entry.ext[ni - 8] = name_83[ni];
        ni = ni + 1;
    }
    entry.attr = ATTR_ARCHIVE;
    set_entry_first_cluster(&entry, first_cluster);
    entry.file_size = len;
    write_entry_at(slot_lba, slot_off, &entry);
    return true;
}

bool fat32_delete_file(const char* path) {
    if (!g_fat32.initialized) {
        return false;
    }
    u32 parent_cluster;
    char name_83[11];
    if (!resolve_parent_dir(path, &parent_cluster, name_83)) {
        return false;
    }
    fat32_dir_entry e;
    u32 lba, off;
    if (!find_entry_in_dir(parent_cluster, name_83, &e, &lba, &off)) {
        return false;
    }
    u8 sector[512];
    if (!ata_read_sector_drive(g_fat32.drive, lba, sector)) {
        return false;
    }
    sector[off] = (u8) 0xE5;
    ata_write_sector_drive(g_fat32.drive, lba, sector);
    u32 first = entry_first_cluster(&e);
    if (first >= 2) {
        free_cluster_chain(first);
    }
    return true;
}

bool fat32_create_dir(const char* path) {
    if (!g_fat32.initialized) {
        return false;
    }
    u32 parent_cluster;
    char name_83[11];
    if (!resolve_parent_dir(path, &parent_cluster, name_83)) {
        return false;
    }
    fat32_dir_entry existing;
    u32 existing_lba, existing_off;
    if (find_entry_in_dir(parent_cluster, name_83, &existing, &existing_lba, &existing_off)) {
        return false;
    }
    u32 new_cluster = alloc_cluster();
    if (new_cluster == 0) {
        return false;
    }
    u32 slot_lba, slot_off;
    if (!find_free_dir_slot(parent_cluster, &slot_lba, &slot_off)) {
        free_cluster_chain(new_cluster);
        return false;
    }
    fat32_dir_entry entry;
    int z = 0;
    u8* entry_bytes = (u8*) &entry;
    while (z < (int) sizeof(entry)) {
        entry_bytes[z] = 0;
        z = z + 1;
    }
    int ni = 0;
    while (ni < 8) {
        entry.name[ni] = name_83[ni];
        ni = ni + 1;
    }
    while (ni < 11) {
        entry.ext[ni - 8] = name_83[ni];
        ni = ni + 1;
    }
    entry.attr = ATTR_DIRECTORY;
    set_entry_first_cluster(&entry, new_cluster);
    entry.file_size = 0;
    write_entry_at(slot_lba, slot_off, &entry);
    return true;
}

bool fat32_list_entry(const char* dir_path, int index, char* name_out, u32* size_out, bool* is_dir_out) {
    if (!g_fat32.initialized) {
        return false;
    }
    u32 dir_cluster;
    if (!resolve_dir(dir_path, &dir_cluster)) {
        return false;
    }
    int seen = 0;
    u32 cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT32_EOC - 8) {
        u32 s = 0;
        while (s < g_fat32.sectors_per_cluster) {
            u8 sector[512];
            if (!ata_read_sector_drive(g_fat32.drive, cluster_to_lba(cluster) + s, sector)) {
                return false;
            }
            u32 off = 0;
            while (off < 512) {
                fat32_dir_entry* e = (fat32_dir_entry*) &sector[off];
                if (e->name[0] == 0x00) {
                    return false;
                }
                if ((u8) e->name[0] != 0xE5 && (e->attr & ATTR_LONG_NAME) != ATTR_LONG_NAME
                    && (e->attr & ATTR_VOLUME_ID) == 0) {
                    if (seen == index) {
                        entry_to_display_name(e, name_out);
                        *size_out = e->file_size;
                        *is_dir_out = (e->attr & ATTR_DIRECTORY) != 0;
                        return true;
                    }
                    seen = seen + 1;
                }
                off = off + 32;
            }
            s = s + 1;
        }
        cluster = get_fat_entry(cluster);
    }
    return false;
}

bool fat32_stat_file(const char* path, u32* size_out, bool* is_dir_out) {
    if (!g_fat32.initialized) {
        return false;
    }
    u32 parent_cluster;
    char name_83[11];
    if (!resolve_parent_dir(path, &parent_cluster, name_83)) {
        return false;
    }
    fat32_dir_entry e;
    u32 lba, off;
    if (!find_entry_in_dir(parent_cluster, name_83, &e, &lba, &off)) {
        return false;
    }
    *size_out = e.file_size;
    *is_dir_out = (e.attr & ATTR_DIRECTORY) != 0;
    return true;
}
