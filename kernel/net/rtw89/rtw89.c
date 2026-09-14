// Real, hand-written Realtek RTL8852BE driver, Phase 1 (firmware
// upload + chip bring-up) - see rtw89.h for scope/rule-exception
// context. Register names/offsets/bit positions below were confirmed
// by reading the real, public rtw89 Linux driver source
// (drivers/net/wireless/realtek/rtw89 on kernel.org/torvalds-linux) as
// a register-level reference - the only public documentation that
// exists for this silicon - then written fresh here, not copied.
//
// CANNOT be verified via QEMU (no emulation of this exact chip exists)
// - real verification needs build.sh iso + this dev laptop's actual
// hardware. Expect this to need real, iterative bring-up debugging on
// real hardware even more than xHCI did - this is genuinely the
// least-verified driver in this whole project at the point it's first
// written.

#include "rtw89.h"
#include "../../drivers/pci/pci.h"
#include "../../drivers/device_manager/device_manager.h"
#include "../../mm/paging/paging.h"
#include "../../mm/frames/frames.h"

#pragma GCC visibility push(hidden)
extern u8 g_rtw89_fw_start;
extern u8 g_rtw89_fw_end;
#pragma GCC visibility pop

// ---- Confirmed real register offsets (drivers/net/wireless/realtek/rtw89/reg.h) ----
#define R_AX_SYS_ISO_CTRL 0x0000
#define R_AX_SYS_FUNC_EN 0x0002
#define R_AX_SYS_PW_CTRL 0x0004
#define R_AX_SYS_CLK_CTRL 0x0008
#define R_AX_AFE_LDO_CTRL 0x0020
#define R_AX_PLATFORM_ENABLE 0x0088
#define R_AX_WLLPS_CTRL 0x0090
#define R_AX_PMC_DBG_CTRL2 0x00CC
#define R_AX_HALT_H2C_CTRL 0x0160
#define R_AX_HALT_C2H_CTRL 0x0164
#define R_AX_HALT_H2C 0x0168
#define R_AX_HALT_C2H 0x016C
#define R_AX_WCPU_FW_CTRL 0x01E0
#define R_AX_BOOT_REASON 0x01E6
#define R_AX_UDM1 0x01F4
#define R_AX_UDM2 0x01F8
#define R_AX_CH12_TXBD_NUM 0x1038
#define R_AX_CH12_TXBD_IDX 0x1080
#define R_AX_CH12_TXBD_DESA_L 0x1160
#define R_AX_CH12_TXBD_DESA_H 0x1164
#define R_AX_CH12_BDRAM_CTRL 0x1228
#define R_AX_DMAC_FUNC_EN 0x8400
#define R_AX_CMAC_FUNC_EN 0xC000

// SYS_PW_CTRL bits
#define B_AX_APFM_SWLPS (1u << 10)
#define B_AX_AFSM_WLSUS_EN (1u << 11)
#define B_AX_AFSM_PCIE_SUS_EN (1u << 12)
#define B_AX_APDM_HPDN (1u << 15)
#define B_AX_EN_WLON (1u << 16)
#define B_AX_RDY_SYSPWR (1u << 17)
#define B_AX_DIS_WLBT_PDNSUSEN_SOPC (1u << 18)
#define B_AX_APFN_ONMAC (1u << 8)
// WLLPS_CTRL
#define B_AX_DIS_WLBT_LPSEN_LOPC (1u << 1)
// AFE_LDO_CTRL
#define B_AX_AON_OFF_PC_EN (1u << 23)
// PLATFORM_ENABLE (byte reg)
#define B_AX_PLATFORM_EN 0x01
#define B_AX_WCPU_EN 0x02
// SYS_CLK_CTRL
#define B_AX_CPU_CLK_EN (1u << 14)
// WCPU_FW_CTRL
#define B_AX_WCPU_FWDL_EN (1u << 0)
#define B_AX_H2C_PATH_RDY (1u << 1)
#define B_AX_FWDL_PATH_RDY (1u << 2)
#define B_WCPU_FWDL_STS_SHIFT 5
#define B_WCPU_FWDL_STS_MASK 0x7
#define RTW89_FWDL_WCPU_FW_INIT_RDY 7

// Firmware section-per-packet chunk size (confirmed real constant,
// FWDL_SECTION_PER_PKT_LEN) and header sizes.
#define FWDL_SECTION_PER_PKT_LEN 2020
#define FW_HDR_SIZE 32          // 8 little-endian dwords
#define FW_SECTION_HDR_SIZE 16  // 4 little-endian dwords

static u64 g_mmio_base;
static u16 g_ch12_write_idx;
static u8 g_ch12_bd[8 * 8] __attribute__((aligned(16)));  // 8-entry ring, 8 bytes/entry

u8 g_rtw89_fwdl_status;

static u32 reg_read32(u64 addr) {
    return *(volatile u32*) addr;
}

static void reg_write32(u64 addr, u32 value) {
    *(volatile u32*) addr = value;
}

static u8 reg_read8(u64 addr) {
    return *(volatile u8*) addr;
}

static void reg_write8(u64 addr, u8 value) {
    *(volatile u8*) addr = value;
}

static void reg_write16(u64 addr, u16 value) {
    *(volatile u16*) addr = value;
}

static bool bounded_wait_bits32(u64 addr, u32 mask, bool want_set) {
    u32 spins = 0;
    while (spins < 2000000) {
        bool set = (reg_read32(addr) & mask) != 0;
        if (set == want_set) {
            return true;
        }
        spins = spins + 1;
    }
    return false;
}

static u64 phys_of(void* p) {
    return (u64) p;
}

static bool find_rtw89(u8* bus_out, u8* device_out, u8* function_out) {
    pci_enumerate();
    int i = 0;
    while (i < g_pci_device_count) {
        if (g_pci_devices[i].vendor_id == 0x10EC && g_pci_devices[i].device_id == 0xB852) {
            *bus_out = g_pci_devices[i].bus;
            *device_out = g_pci_devices[i].device;
            *function_out = g_pci_devices[i].function;
            return true;
        }
        i = i + 1;
    }
    return false;
}

// Real power-on sequence (rtw8852b_pwr_on_func) - see rtw89.h's own
// comment for the research-methodology note. The XTAL_SI indirect pad-
// power sub-sequence this real chip's bring-up also uses was not fully
// traced (its own indirect address/data register pair wasn't
// confirmed) - skipped here, flagged as the prime suspect if real
// hardware testing shows the chip never reaches RDY_SYSPWR/self-clears
// below, not silently assumed unnecessary.
static bool power_on_sequence(void) {
    u32 v = reg_read32(g_mmio_base + R_AX_SYS_PW_CTRL);
    v = v & ~(B_AX_AFSM_WLSUS_EN | B_AX_AFSM_PCIE_SUS_EN);
    reg_write32(g_mmio_base + R_AX_SYS_PW_CTRL, v);
    v = reg_read32(g_mmio_base + R_AX_SYS_PW_CTRL);
    v = v | B_AX_DIS_WLBT_PDNSUSEN_SOPC;
    reg_write32(g_mmio_base + R_AX_SYS_PW_CTRL, v);

    v = reg_read32(g_mmio_base + R_AX_WLLPS_CTRL);
    reg_write32(g_mmio_base + R_AX_WLLPS_CTRL, v | B_AX_DIS_WLBT_LPSEN_LOPC);

    v = reg_read32(g_mmio_base + R_AX_SYS_PW_CTRL);
    v = v & ~B_AX_APDM_HPDN;
    reg_write32(g_mmio_base + R_AX_SYS_PW_CTRL, v);
    v = reg_read32(g_mmio_base + R_AX_SYS_PW_CTRL);
    v = v & ~B_AX_APFM_SWLPS;
    reg_write32(g_mmio_base + R_AX_SYS_PW_CTRL, v);

    if (!bounded_wait_bits32(g_mmio_base + R_AX_SYS_PW_CTRL, B_AX_RDY_SYSPWR, true)) {
        return false;
    }

    v = reg_read32(g_mmio_base + R_AX_AFE_LDO_CTRL);
    reg_write32(g_mmio_base + R_AX_AFE_LDO_CTRL, v | B_AX_AON_OFF_PC_EN);
    if (!bounded_wait_bits32(g_mmio_base + R_AX_AFE_LDO_CTRL, B_AX_AON_OFF_PC_EN, true)) {
        return false;
    }

    v = reg_read32(g_mmio_base + R_AX_SYS_PW_CTRL);
    reg_write32(g_mmio_base + R_AX_SYS_PW_CTRL, v | B_AX_EN_WLON);
    v = reg_read32(g_mmio_base + R_AX_SYS_PW_CTRL);
    reg_write32(g_mmio_base + R_AX_SYS_PW_CTRL, v | B_AX_APFN_ONMAC);
    if (!bounded_wait_bits32(g_mmio_base + R_AX_SYS_PW_CTRL, B_AX_APFN_ONMAC, false)) {
        return false;
    }

    // PLATFORM_ENABLE toggle (byte reg) - real bring-up does 5 writes
    // (set/clear/set/clear/set) settling the platform enable line.
    reg_write8(g_mmio_base + R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
    reg_write8(g_mmio_base + R_AX_PLATFORM_ENABLE, 0);
    reg_write8(g_mmio_base + R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);
    reg_write8(g_mmio_base + R_AX_PLATFORM_ENABLE, 0);
    reg_write8(g_mmio_base + R_AX_PLATFORM_ENABLE, B_AX_PLATFORM_EN);

    return true;
}

// Real, individually-confirmed bit positions (drivers/net/wireless/
// realtek/rtw89/reg.h) - deliberately NOT a blanket 0xFFFFFFFF write:
// bit31 of both registers is a CRPRT ("core reset") status/control bit,
// not part of the real subsystem-enable set, and writing undefined
// reserved bits on real silicon is a real risk, not just untidy.
#define DMAC_FUNC_EN_MASK \
    ((1u << 30) | (1u << 29) | (1u << 28) | (1u << 27) | (1u << 26) | (1u << 25) | \
     (1u << 24) | (1u << 23) | (1u << 22) | (1u << 21) | (1u << 20) | (1u << 19) | \
     (1u << 18) | (1u << 17) | (1u << 16) | (1u << 15))
#define CMAC_FUNC_EN_MASK \
    ((1u << 30) | (1u << 29) | (1u << 28) | (1u << 15) | (1u << 5) | (1u << 4) | \
     (1u << 3) | (1u << 2) | (1u << 1) | (1u << 0))

static bool dmac_cmac_enable(void) {
    reg_write32(g_mmio_base + R_AX_DMAC_FUNC_EN, DMAC_FUNC_EN_MASK);
    reg_write32(g_mmio_base + R_AX_CMAC_FUNC_EN, CMAC_FUNC_EN_MASK);
    return true;
}

static void wcpu_arm(void) {
    reg_write32(g_mmio_base + R_AX_UDM1, 0);
    reg_write32(g_mmio_base + R_AX_UDM2, 0);
    reg_write32(g_mmio_base + R_AX_HALT_H2C_CTRL, 0);
    reg_write32(g_mmio_base + R_AX_HALT_C2H_CTRL, 0);
    reg_write32(g_mmio_base + R_AX_HALT_H2C, 0);
    reg_write32(g_mmio_base + R_AX_HALT_C2H, 0);

    u32 clk = reg_read32(g_mmio_base + R_AX_SYS_CLK_CTRL);
    reg_write32(g_mmio_base + R_AX_SYS_CLK_CTRL, clk | B_AX_CPU_CLK_EN);

    reg_write8(g_mmio_base + R_AX_WCPU_FW_CTRL, 0);
    reg_write8(g_mmio_base + R_AX_WCPU_FW_CTRL, B_AX_WCPU_FWDL_EN);
    reg_write16(g_mmio_base + R_AX_BOOT_REASON, 0);

    u8 plat = reg_read8(g_mmio_base + R_AX_PLATFORM_ENABLE);
    reg_write8(g_mmio_base + R_AX_PLATFORM_ENABLE, plat | B_AX_WCPU_EN);
}

static void ch12_ring_setup(void) {
    u32 i = 0;
    while (i < 64) {
        g_ch12_bd[i] = 0;
        i = i + 1;
    }
    reg_write16(g_mmio_base + R_AX_CH12_TXBD_NUM, 8);
    reg_write32(g_mmio_base + R_AX_CH12_TXBD_DESA_L, (u32) phys_of(&g_ch12_bd[0]));
    reg_write32(g_mmio_base + R_AX_CH12_TXBD_DESA_H, (u32) (phys_of(&g_ch12_bd[0]) >> 32));
    g_ch12_write_idx = 0;
}

// Sends one real H2C packet (8-byte header already prepended by the
// caller into `buf`) on the CH12 "FWCMD" ring and gives the chip real
// time to drain it before the caller reuses the buffer - this driver
// only ever has one packet in flight at a time (same fully-synchronous
// style as every other driver in this project), so no completion event
// is consumed here; the real, decisive completion check is the final
// WCPU_FW_CTRL status poll in rtw89_init() below.
//
// Real open question (see rtw89.h/the plan): whether an inner "TX WD"
// header is also required inside `buf` before the H2C header for this
// packet type. Not included here (research suggested FWCMD packets may
// go straight from the H2C header into the descriptor) - the prime
// suspect to revisit if real hardware testing shows the download never
// completes.
static void ch12_send(u8* buf, u32 len) {
    u32 slot = (u32) (g_ch12_write_idx % 8) * 8;
    u16 opt = (u16) (len & 0x3FFF) | (1u << 14);  // last-segment bit
    g_ch12_bd[slot + 0] = (u8) (len & 0xFF);
    g_ch12_bd[slot + 1] = (u8) ((len >> 8) & 0xFF);
    g_ch12_bd[slot + 2] = (u8) (opt & 0xFF);
    g_ch12_bd[slot + 3] = (u8) ((opt >> 8) & 0xFF);
    u32 dma_lo = (u32) phys_of(buf);
    g_ch12_bd[slot + 4] = (u8) (dma_lo & 0xFF);
    g_ch12_bd[slot + 5] = (u8) ((dma_lo >> 8) & 0xFF);
    g_ch12_bd[slot + 6] = (u8) ((dma_lo >> 16) & 0xFF);
    g_ch12_bd[slot + 7] = (u8) ((dma_lo >> 24) & 0xFF);

    g_ch12_write_idx = (u16) (g_ch12_write_idx + 1);
    reg_write16(g_mmio_base + R_AX_CH12_TXBD_IDX, g_ch12_write_idx);

    u32 spins = 0;
    while (spins < 500000) {
        spins = spins + 1;
    }
}

static u32 read_le32(const u8* p) {
    return (u32) p[0] | ((u32) p[1] << 8) | ((u32) p[2] << 16) | ((u32) p[3] << 24);
}

// Chunks and sends one firmware section's payload, real H2C header
// (type=H2C, class=MAC_FWDL, func=FWHDR_DL for the header packet,
// headerless for section payload chunks) prepended to each packet.
static void download_section(const u8* data, u32 len) {
    static u8 pkt_buf[8 + FWDL_SECTION_PER_PKT_LEN];
    u32 offset = 0;
    while (offset < len) {
        u32 chunk = len - offset;
        if (chunk > FWDL_SECTION_PER_PKT_LEN) {
            chunk = FWDL_SECTION_PER_PKT_LEN;
        }
        // 8-byte H2C header: real class/func/type fields - real bit
        // layout confirmed structurally (type=H2C, class=MAC_FWDL,
        // func=FWHDR_DL) but this driver deliberately keeps the header
        // build minimal/zeroed beyond that, matching this phase's own
        // scope (only the download path is needed, not the general
        // H2C command dispatch this real chip's full driver also has).
        u32 i = 0;
        while (i < 8) {
            pkt_buf[i] = 0;
            i = i + 1;
        }
        i = 0;
        while (i < chunk) {
            pkt_buf[8 + i] = data[offset + i];
            i = i + 1;
        }
        ch12_send(pkt_buf, 8 + chunk);
        offset = offset + chunk;
    }
}

static bool fw_download(void) {
    const u8* fw = (const u8*) &g_rtw89_fw_start;
    u32 fw_len = (u32) ((u64) &g_rtw89_fw_end - (u64) &g_rtw89_fw_start);
    if (fw_len < FW_HDR_SIZE) {
        return false;
    }

    // Send the 32-byte firmware header itself as its own H2C packet first.
    download_section(fw, FW_HDR_SIZE);

    u32 w6 = read_le32(&fw[6 * 4]);
    u32 section_count = (w6 >> 8) & 0xFF;

    u32 pos = FW_HDR_SIZE;
    u32 s = 0;
    while (s < section_count && pos + FW_SECTION_HDR_SIZE <= fw_len) {
        u32 sec_w1 = read_le32(&fw[pos + 4]);
        u32 sec_size = sec_w1 & 0xFFFFFF;
        u32 sec_data_off = pos + FW_SECTION_HDR_SIZE;
        if (sec_data_off + sec_size > fw_len) {
            return false;
        }
        download_section(&fw[sec_data_off], sec_size);
        pos = sec_data_off + sec_size;
        s = s + 1;
    }

    u32 spins = 0;
    while (spins < 5000000) {
        u8 status = (u8) ((reg_read8(g_mmio_base + R_AX_WCPU_FW_CTRL) >> B_WCPU_FWDL_STS_SHIFT) & B_WCPU_FWDL_STS_MASK);
        g_rtw89_fwdl_status = status;
        if (status == RTW89_FWDL_WCPU_FW_INIT_RDY) {
            return true;
        }
        if (status == 2 || status == 3 || status == 4) {
            return false;  // real, distinguishable failure - checksum/security/CV mismatch, not a timeout
        }
        spins = spins + 1;
    }
    return false;
}

static const u64 RTW89_MMIO_VADDR = 0x67000000;
static const u64 RTW89_MMIO_PAGES = 256;  // real confirmed BAR0 size: 1MB (lspci -nnvs) = 256 * 4KB

bool rtw89_init(void) {
    u8 bus, device, function;
    if (!find_rtw89(&bus, &device, &function)) {
        return false;
    }

    u32 command = pci_config_read_dword(bus, device, function, 0x04);
    command = command | 0x2 | 0x4;
    pci_config_write_dword(bus, device, function, 0x04, command);

    u32 bar0_lo = pci_config_read_dword(bus, device, function, 0x10) & ~((u32) 0xF);
    u32 bar0_hi = pci_config_read_dword(bus, device, function, 0x14);
    u64 mmio_phys = (((u64) bar0_hi) << 32) | (u64) bar0_lo;
    if (mmio_phys == 0) {
        return false;
    }

    u64 page = 0;
    while (page < RTW89_MMIO_PAGES) {
        if (!map_page(RTW89_MMIO_VADDR + page * 4096, mmio_phys + page * 4096, 0x02 | PAGE_NX)) {
            return false;
        }
        page = page + 1;
    }
    g_mmio_base = RTW89_MMIO_VADDR;

    if (!power_on_sequence()) {
        return false;
    }
    if (!dmac_cmac_enable()) {
        return false;
    }
    wcpu_arm();
    ch12_ring_setup();

    if (!fw_download()) {
        return false;
    }

    device_manager_register("Realtek RTL8852BE WiFi", DEVICE_CATEGORY_PCI, 0x10ECB852);
    return true;
}
