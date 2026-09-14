// Real block-device dispatch layer: minifs.c/fat32.c address logical
// devices (BLOCKDEV_SYSTEM/BLOCKDEV_FAT32), not a specific controller.
// blockdev_init() decides once, at boot, whether NVMe or legacy ATA is
// the real backend - the same plain if/else-on-a-tag idiom vfs.c's own
// BACKEND_MINIFS/BACKEND_DEVICE/etc dispatch already uses (this
// codebase has no function-pointer dispatch anywhere).

#include "blockdev.h"
#include "../ata/ata.h"
#include "../nvme/nvme.h"

static bool g_use_nvme;

void blockdev_init(void) {
    g_use_nvme = nvme_init();
}

bool blockdev_read_sector(u8 dev, u32 lba, u8* buffer) {
    if (g_use_nvme) {
        u8 nsid = (dev == BLOCKDEV_SYSTEM) ? 1 : 2;
        return nvme_read_sector(nsid, lba, buffer);
    }
    u8 drive = (dev == BLOCKDEV_SYSTEM) ? 0 : 1;
    return ata_read_sector_drive(drive, lba, buffer);
}

bool blockdev_write_sector(u8 dev, u32 lba, u8* buffer) {
    if (g_use_nvme) {
        u8 nsid = (dev == BLOCKDEV_SYSTEM) ? 1 : 2;
        return nvme_write_sector(nsid, lba, buffer);
    }
    u8 drive = (dev == BLOCKDEV_SYSTEM) ? 0 : 1;
    return ata_write_sector_drive(drive, lba, buffer);
}
