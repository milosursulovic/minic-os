#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real, hand-written FAT32 driver (Faza I point 6, item 15) - a genuinely
// separate on-disk format from kernel/fs/minifs, mounted on its own
// drive (kernel/fs/ata's new drive-select support) to prove
// kernel/fs/vfs/vfs.c's backend dispatch really is pluggable. Real,
// deliberate scope limits, matching this codebase's own habit of
// documenting simplifications instead of hiding them:
//   - 8.3 short names only - no VFAT long-filename entries (attribute
//     0x0F) are ever written, and any found on disk are skipped over
//     when reading a directory.
//   - No timestamp maintenance - creation/write time/date fields are
//     written as 0 and never read back for anything.
//   - No FSInfo-sector free-cluster-count caching - every free-cluster
//     search is a real O(n) FAT scan from cluster 2. A performance
//     simplification, not a correctness one.
//   - Write is create-only-fails-if-exists, same convention
//     kernel/fs/minifs/minifs.c's fs_write_file already has - overwrite
//     is caller-side delete-then-write, the established idiom
//     everywhere else in this codebase (kernel/fs/vfs/vfs.c's own
//     callers already do this for MiniFS).
// Must call fat32_init() once (kmain.c, before any other call) - real
// on-disk state (BPB-derived layout) has to exist before any path can
// resolve to anything.
bool fat32_init(u8 drive);

// Returns byte count read, -1 if not found (or resolves to a directory),
// -2 if too big for max_len.
int fat32_read_file(const char* path, u8* out_buffer, u32 max_len);
// Fails if path already exists (see the create-only note above) or any
// parent component doesn't exist / isn't a directory.
bool fat32_write_file(const char* path, u8* data, u32 len);
bool fat32_delete_file(const char* path);
// Fails if the name already exists or any parent component doesn't
// exist / isn't a directory.
bool fat32_create_dir(const char* path);
// Same "raw index within dir_path, false past the last used slot"
// contract kernel/fs/minifs/minifs.c's fs_list_entry already uses.
bool fat32_list_entry(const char* dir_path, int index, char* name_out, u32* size_out, bool* is_dir_out);
// No owner/mode concept - real FAT32 has none on disk, so there's
// nothing honest to report there (unlike kernel/fs/tmpfs's vfs_stat
// wrapper, which reports honest zero defaults for the same reason).
bool fat32_stat_file(const char* path, u32* size_out, bool* is_dir_out);

#pragma GCC visibility pop
