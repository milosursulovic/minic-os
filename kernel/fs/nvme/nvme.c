// Real, hand-written NVMe driver (NVMe spec 1.4 Controller Registers +
// NVM command set) - PCI-discovered, MMIO register interface (like
// net/e1000/e1000.c, unlike port-I/O fs/ata/ata.c). Polling only (no
// interrupts - same scope limit kernel/drivers/usb/uhci.c already set
// for USB), one admin queue + one I/O queue pair, queue depth 2 (the
// spec minimum - this driver is always exactly one command in flight,
// matching ata.c's own fully synchronous one-at-a-time model, so more
// depth would never actually be used).

#include "nvme.h"
#include "../../drivers/pci/pci.h"
#include "../../drivers/device_manager/device_manager.h"
#include "../../mm/paging/paging.h"
#include "../../mm/frames/frames.h"

// ---- Controller register offsets (BAR0, spec-fixed) ----
static const u32 NVME_REG_CAP = 0x00;    // 8 bytes
static const u32 NVME_REG_CC = 0x14;     // 4 bytes
static const u32 NVME_REG_CSTS = 0x1C;   // 4 bytes
static const u32 NVME_REG_AQA = 0x24;    // 4 bytes
static const u32 NVME_REG_ASQ = 0x28;    // 8 bytes
static const u32 NVME_REG_ACQ = 0x30;    // 8 bytes
static const u32 NVME_DOORBELL_BASE = 0x1000;

static const u64 NVME_MMIO_VADDR = 0x65000000;
static const u64 NVME_MMIO_PAGES = 4;

static const u32 QUEUE_DEPTH = 2;  // spec minimum - always exactly one command in flight here

static u64 g_nvme_mmio_base;
static u32 g_doorbell_stride;  // bytes

static u64 g_asq_phys, g_acq_phys;
static u16 g_asq_tail, g_acq_head, g_acq_phase;
static u64 g_iosq_phys, g_iocq_phys;
static u16 g_iosq_tail, g_iocq_head, g_iocq_phase;

// 64-byte Submission Queue Entry (every NVMe command, admin or I/O).
typedef struct __attribute__((packed)) {
    u32 cdw0;   // opcode (bits 0-7) | cid (bits 16-31, set by the submitter)
    u32 nsid;
    u32 cdw2;
    u32 cdw3;
    u64 mptr;   // metadata pointer - unused, 0
    u64 prp1;
    u64 prp2;
    u32 cdw10;
    u32 cdw11;
    u32 cdw12;
    u32 cdw13;
    u32 cdw14;
    u32 cdw15;
} nvme_command;

// 16-byte Completion Queue Entry.
typedef struct __attribute__((packed)) {
    u32 dw0;
    u32 dw1;
    u16 sq_head;
    u16 sq_id;
    u16 cid;
    u16 status;  // bit0 = phase tag, bits 1-15 = status field (0 = success)
} nvme_completion;

static u32 nvme_read32(u32 offset) {
    volatile u32* reg = (volatile u32*) (g_nvme_mmio_base + (u64) offset);
    return *reg;
}

static void nvme_write32(u32 offset, u32 value) {
    volatile u32* reg = (volatile u32*) (g_nvme_mmio_base + (u64) offset);
    *reg = value;
}

static u64 nvme_read64(u32 offset) {
    u64 lo = nvme_read32(offset);
    u64 hi = nvme_read32(offset + 4);
    return lo | (hi << 32);
}

static void nvme_write64(u32 offset, u64 value) {
    nvme_write32(offset, (u32) value);
    nvme_write32(offset + 4, (u32) (value >> 32));
}

static void zero_page(void* page) {
    u8* p = (u8*) page;
    u32 i = 0;
    while (i < 4096) {
        p[i] = 0;
        i = i + 1;
    }
}

// Bounded spin (never hang forever if the controller is wedged) - same
// convention ata_wait_ready()/ata_wait_drq() already use.
static bool nvme_wait_ready(bool want_ready) {
    u32 spins = 0;
    while (spins < 1000000) {
        u32 csts = nvme_read32(NVME_REG_CSTS);
        bool ready = (csts & 0x1) != 0;
        if (ready == want_ready) {
            return true;
        }
        spins = spins + 1;
    }
    return false;
}

static bool find_nvme_device(u8* bus_out, u8* device_out, u8* function_out,
                              u16* vendor_out, u16* device_id_out) {
    pci_enumerate();
    int i = 0;
    while (i < g_pci_device_count) {
        if (g_pci_devices[i].class_code == 0x01 && g_pci_devices[i].subclass == 0x08) {
            *bus_out = g_pci_devices[i].bus;
            *device_out = g_pci_devices[i].device;
            *function_out = g_pci_devices[i].function;
            *vendor_out = g_pci_devices[i].vendor_id;
            *device_id_out = g_pci_devices[i].device_id;
            return true;
        }
        i = i + 1;
    }
    return false;
}

// Shared by controller bring-up (queue-create admin commands) - writes
// the command to the admin submission queue, rings its doorbell, polls
// the admin completion queue for the phase-bit flip, acks it. Returns
// whether the command's own status field reported success.
static bool nvme_submit_admin(nvme_command* cmd) {
    nvme_command* sq = (nvme_command*) g_asq_phys;
    cmd->cdw0 = (cmd->cdw0 & 0xFFFF) | ((u32) g_asq_tail << 16);
    sq[g_asq_tail] = *cmd;
    g_asq_tail = (u16) ((g_asq_tail + 1) % QUEUE_DEPTH);
    nvme_write32(NVME_DOORBELL_BASE + 0u * g_doorbell_stride, g_asq_tail);

    nvme_completion* cq = (nvme_completion*) g_acq_phys;
    u32 spins = 0;
    while (spins < 10000000) {
        nvme_completion c = cq[g_acq_head];
        if ((c.status & 0x1) == g_acq_phase) {
            g_acq_head = (u16) ((g_acq_head + 1) % QUEUE_DEPTH);
            if (g_acq_head == 0) {
                g_acq_phase = g_acq_phase ^ 1;
            }
            nvme_write32(NVME_DOORBELL_BASE + 1u * g_doorbell_stride, g_acq_head);
            return ((c.status >> 1) & 0x7FFF) == 0;
        }
        spins = spins + 1;
    }
    return false;
}

static bool nvme_create_io_queues(void) {
    void* iocq = alloc_frame();
    void* iosq = alloc_frame();
    if (iocq == 0 || iosq == 0) {
        return false;
    }
    zero_page(iocq);
    zero_page(iosq);
    g_iocq_phys = (u64) iocq;
    g_iosq_phys = (u64) iosq;
    g_iocq_head = 0;
    g_iocq_phase = 1;
    g_iosq_tail = 0;

    nvme_command create_cq = {0};
    create_cq.cdw0 = 0x05;  // Create I/O Completion Queue
    create_cq.prp1 = g_iocq_phys;
    create_cq.cdw10 = ((QUEUE_DEPTH - 1) << 16) | 1;  // (size-1)<<16 | qid=1
    create_cq.cdw11 = 0x1;  // PC=1 (physically contiguous), IEN=0 (no interrupts)
    if (!nvme_submit_admin(&create_cq)) {
        return false;
    }

    nvme_command create_sq = {0};
    create_sq.cdw0 = 0x01;  // Create I/O Submission Queue
    create_sq.prp1 = g_iosq_phys;
    create_sq.cdw10 = ((QUEUE_DEPTH - 1) << 16) | 1;  // qid=1
    create_sq.cdw11 = (1u << 16) | 0x1;  // cqid=1<<16, PC=1
    if (!nvme_submit_admin(&create_sq)) {
        return false;
    }

    return true;
}

bool nvme_init(void) {
    u8 bus, device, function;
    u16 vendor_id, device_id;
    if (!find_nvme_device(&bus, &device, &function, &vendor_id, &device_id)) {
        return false;
    }

    u32 command = pci_config_read_dword(bus, device, function, 0x04);
    command = command | 0x2 | 0x4;  // memory space + bus master
    pci_config_write_dword(bus, device, function, 0x04, command);

    // NVMe's BAR0 is a real 64-bit memory BAR - combine the low
    // (offset 0x10, masked) and high (offset 0x14) dwords, unlike every
    // other driver here which only ever needed pci_read_bar0()'s 32-bit
    // form.
    u32 bar0_lo = pci_config_read_dword(bus, device, function, 0x10) & ~((u32) 0xF);
    u32 bar0_hi = pci_config_read_dword(bus, device, function, 0x14);
    u64 mmio_phys = (((u64) bar0_hi) << 32) | (u64) bar0_lo;
    if (mmio_phys == 0) {
        return false;
    }

    u64 page = 0;
    while (page < NVME_MMIO_PAGES) {
        u64 vaddr = NVME_MMIO_VADDR + (page * 4096);
        u64 paddr = mmio_phys + (page * 4096);
        if (!map_page(vaddr, paddr, 0x02 | PAGE_NX)) {
            return false;
        }
        page = page + 1;
    }
    g_nvme_mmio_base = NVME_MMIO_VADDR;

    u64 cap = nvme_read64(NVME_REG_CAP);
    g_doorbell_stride = 4u << ((u32) ((cap >> 32) & 0xF));

    // Reset the controller (CC.EN=0) and wait for CSTS.RDY to drop
    // before reconfiguring it - the same "quiesce before reprogramming"
    // shape vbe_init()'s own DISPI_ENABLE toggle already follows.
    nvme_write32(NVME_REG_CC, 0);
    if (!nvme_wait_ready(false)) {
        return false;
    }

    void* asq = alloc_frame();
    void* acq = alloc_frame();
    if (asq == 0 || acq == 0) {
        return false;
    }
    zero_page(asq);
    zero_page(acq);
    g_asq_phys = (u64) asq;
    g_acq_phys = (u64) acq;
    g_asq_tail = 0;
    g_acq_head = 0;
    g_acq_phase = 1;

    nvme_write32(NVME_REG_AQA, (QUEUE_DEPTH - 1) | ((QUEUE_DEPTH - 1) << 16));
    nvme_write64(NVME_REG_ASQ, g_asq_phys);
    nvme_write64(NVME_REG_ACQ, g_acq_phys);

    // IOSQES=6 (2^6=64B entries), IOCQES=4 (2^4=16B entries), MPS=0
    // (4K pages), AMS=0 (round-robin), CSS=0 (NVM command set), EN=1.
    u32 cc = (6u << 16) | (4u << 20) | 1u;
    nvme_write32(NVME_REG_CC, cc);
    if (!nvme_wait_ready(true)) {
        return false;
    }

    if (!nvme_create_io_queues()) {
        return false;
    }

    device_manager_register("NVMe SSD", DEVICE_CATEGORY_PCI, ((u32) vendor_id << 16) | device_id);
    return true;
}

// Real correctness detail: PRP1 is the buffer's physical address as-is
// (buffer is always a stack array inside the identity-mapped low 1GB,
// so vaddr==paddr already - the same assumption ata.c's own buffers
// rely on implicitly). If the transfer would cross a 4K page boundary
// (a real possibility for a stack-allocated, non-page-aligned buffer),
// PRP2 must point at the next physical page - real linear RAM here, so
// that's genuinely the correct continuation of the same buffer, not a
// separate allocation. Getting this wrong would be a silent,
// alignment-dependent data-corruption bug.
static bool nvme_io_command(u8 opcode, u8 nsid, u32 lba, u8* buffer) {
    u64 addr = (u64) buffer;
    u64 prp2 = 0;
    if ((addr & 0xFFF) + 512 > 4096) {
        prp2 = (addr & ~((u64) 0xFFF)) + 0x1000;
    }

    nvme_command cmd = {0};
    cmd.cdw0 = opcode;
    cmd.nsid = nsid;
    cmd.prp1 = addr;
    cmd.prp2 = prp2;
    cmd.cdw10 = lba;  // starting LBA, low 32 bits
    cmd.cdw11 = 0;    // starting LBA, high 32 bits - always 0, never exceed 4G sectors here
    cmd.cdw12 = 0;    // NLB=0 means 1 logical block (0's-based count)

    nvme_command* sq = (nvme_command*) g_iosq_phys;
    cmd.cdw0 = (cmd.cdw0 & 0xFFFF) | ((u32) g_iosq_tail << 16);
    sq[g_iosq_tail] = cmd;
    g_iosq_tail = (u16) ((g_iosq_tail + 1) % QUEUE_DEPTH);
    nvme_write32(NVME_DOORBELL_BASE + 2u * g_doorbell_stride, g_iosq_tail);

    nvme_completion* cq = (nvme_completion*) g_iocq_phys;
    u32 spins = 0;
    while (spins < 10000000) {
        nvme_completion c = cq[g_iocq_head];
        if ((c.status & 0x1) == g_iocq_phase) {
            bool ok = ((c.status >> 1) & 0x7FFF) == 0;
            g_iocq_head = (u16) ((g_iocq_head + 1) % QUEUE_DEPTH);
            if (g_iocq_head == 0) {
                g_iocq_phase = g_iocq_phase ^ 1;
            }
            nvme_write32(NVME_DOORBELL_BASE + 3u * g_doorbell_stride, g_iocq_head);
            return ok;
        }
        spins = spins + 1;
    }
    return false;
}

bool nvme_read_sector(u8 nsid, u32 lba, u8* buffer) {
    return nvme_io_command(0x02, nsid, lba, buffer);  // opcode 0x02 = Read
}

bool nvme_write_sector(u8 nsid, u32 lba, u8* buffer) {
    return nvme_io_command(0x01, nsid, lba, buffer);  // opcode 0x01 = Write
}
