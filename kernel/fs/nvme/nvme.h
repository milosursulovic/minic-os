#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real, hand-written NVMe driver: PCI-discovered controller bring-up
// (admin queue, one I/O queue pair), polling only (no interrupts - same
// documented scope limit kernel/drivers/usb/uhci.c already set), one
// command in flight at a time (matches kernel/fs/ata/ata.c's own fully
// synchronous style). False if no real NVMe controller is present -
// kernel/fs/blockdev/blockdev.c's real fallback-to-ATA signal.
bool nvme_init(void);
bool nvme_read_sector(u8 nsid, u32 lba, u8* buffer);
bool nvme_write_sector(u8 nsid, u32 lba, u8* buffer);

#pragma GCC visibility pop
