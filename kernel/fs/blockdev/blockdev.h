#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Logical block devices this kernel's filesystems address - not real
// hardware identifiers. blockdev_init() decides once, at boot, which
// real backend (legacy ATA drive index, or an NVMe namespace) each one
// maps to, so minifs.c/fat32.c never need to know or care which.
#define BLOCKDEV_SYSTEM 0  // MiniFS's own backing store (/system, /apps, /users)
#define BLOCKDEV_FAT32  1  // the FAT32-mounted backing store (/fat32)

// Real probe: uses kernel/fs/nvme's nvme_init() if a real NVMe
// controller is found (this laptop's real hardware - no legacy IDE
// controller exists there at all); otherwise falls back to legacy ATA
// drive 0/1 exactly as every existing disk.img/fat32.img QEMU test and
// VirtualBox already use - zero regression when no NVMe is present.
void blockdev_init(void);
bool blockdev_read_sector(u8 dev, u32 lba, u8* buffer);
bool blockdev_write_sector(u8 dev, u32 lba, u8* buffer);

#pragma GCC visibility pop
