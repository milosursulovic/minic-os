#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

bool ata_wait_ready(void);
bool ata_wait_drq(void);
bool ata_read_sector(u32 lba, u8* buffer);
bool ata_write_sector(u32 lba, u8* buffer);
// Real second volume support (Faza I point 6, item 15 - the FAT32
// backend's own drive) - drive 0=master (what ata_read_sector/
// ata_write_sector above always used), 1=slave, same primary channel/
// ports, just the drive-select bit.
bool ata_read_sector_drive(u8 drive, u32 lba, u8* buffer);
bool ata_write_sector_drive(u8 drive, u32 lba, u8* buffer);

#pragma GCC visibility pop
