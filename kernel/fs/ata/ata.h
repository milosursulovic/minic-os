#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

bool ata_wait_ready(void);
bool ata_wait_drq(void);
bool ata_read_sector(u32 lba, u8* buffer);
bool ata_write_sector(u32 lba, u8* buffer);

#pragma GCC visibility pop
