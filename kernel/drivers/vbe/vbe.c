// Bochs/QEMU VBE "DISPI" extension: sets a linear framebuffer mode via
// plain port I/O, no VESA BIOS calls needed (real/unreal mode only, and
// we're already in long mode by the time any driver runs). The LFB's
// physical base comes from the VGA-class PCI device's own BAR0 - same
// discover-then-map_page() pattern net/e1000.c already established for
// its MMIO registers.

#include "vbe.h"
#include "../io/io.h"
#include "../pci/pci.h"
#include "../device_manager/device_manager.h"
#include "../../mm/paging/paging.h"
#include "../../mm/frames/frames.h"
#include "../../gfx/font/font.h"

static const u16 VBE_DISPI_IOPORT_INDEX = 0x01CE;
static const u16 VBE_DISPI_IOPORT_DATA = 0x01CF;

static const u16 VBE_DISPI_INDEX_ID = 0;
static const u16 VBE_DISPI_INDEX_XRES = 1;
static const u16 VBE_DISPI_INDEX_YRES = 2;
static const u16 VBE_DISPI_INDEX_BPP = 3;
static const u16 VBE_DISPI_INDEX_ENABLE = 4;

static const u16 VBE_DISPI_DISABLED = 0x00;
static const u16 VBE_DISPI_ENABLED = 0x01;
static const u16 VBE_DISPI_LFB_ENABLED = 0x40;

// Below this, port 0x1CE/0x1CF isn't a real DISPI interface. NOT
// sufficient on its own though - an unassigned real I/O port typically
// reads back as 0xFFFF (floating bus), which is >= this threshold and
// would pass the check on real hardware even though no DISPI interface
// exists there at all. See the real vendor/device ID check in
// find_vga_device() below, added after this exact false positive let
// vbe_init() proceed on a real Intel HD Graphics laptop, map that GPU's
// real MMIO register aperture (its PCI BAR0, discovered as a genuine
// class-0x03 VGA device) as if it were a Bochs-style linear framebuffer,
// and blast raw pixel writes into real display-controller registers -
// breaking the physical display within seconds. Real hardware simply
// does not implement this Bochs/QEMU-specific "DISPI" register
// interface; only emulated/paravirtualized VGA adapters do.
static const u16 VBE_DISPI_ID_MIN = 0xB0C0;

// The only VGA-class devices known to implement the Bochs DISPI register
// interface this driver relies on: QEMU/Bochs standard VGA (vendor
// 0x1234 - the well-known "Plex86/Bochs VGA" ID QEMU's `-vga std` uses)
// and VirtualBox's VBoxVGA (vendor 0x80EE), which emulates the same
// DISPI registers for compatibility. Any other vendor (Intel/AMD/Nvidia
// real hardware included) is real silicon this driver must never touch.
static const u16 VBE_VENDOR_QEMU_BOCHS = 0x1234;
static const u16 VBE_VENDOR_VIRTUALBOX = 0x80EE;

static const u64 FB_VADDR = 0x70000000;

u64 g_fb_vaddr;
u32 g_fb_width;
u32 g_fb_height;
u32 g_fb_pitch;
bool g_fb_enabled;

static u32 g_fb_lfb_phys;

static void vbe_write_reg(u16 index, u16 value) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    outw(VBE_DISPI_IOPORT_DATA, value);
}

u16 vbe_read_reg(u16 index) {
    outw(VBE_DISPI_IOPORT_INDEX, index);
    return inw(VBE_DISPI_IOPORT_DATA);
}

u32 vbe_lfb_phys(void) {
    return g_fb_lfb_phys;
}

static bool find_vga_device(u8* bus_out, u8* device_out, u8* function_out) {
    pci_enumerate();
    int i = 0;
    while (i < g_pci_device_count) {
        if (g_pci_devices[i].class_code == 0x03
            && (g_pci_devices[i].vendor_id == VBE_VENDOR_QEMU_BOCHS
                || g_pci_devices[i].vendor_id == VBE_VENDOR_VIRTUALBOX)) {
            *bus_out = g_pci_devices[i].bus;
            *device_out = g_pci_devices[i].device;
            *function_out = g_pci_devices[i].function;
            return true;
        }
        i = i + 1;
    }
    return false;
}

// Shared by vbe_init() and vbe_init_multiboot() - maps `total_bytes`
// worth of pages starting at physical `phys` to FB_VADDR, writable/non-exec.
static bool map_framebuffer_pages(u64 phys, u32 total_bytes) {
    u32 pages = (total_bytes + 4095) / 4096;
    u32 page = 0;
    while (page < pages) {
        u64 vaddr = FB_VADDR + ((u64) page * 4096);
        u64 paddr = phys + ((u64) page * 4096);
        if (!map_page(vaddr, paddr, 0x02 | PAGE_NX)) {  // framebuffer: writable, non-exec
            return false;
        }
        page = page + 1;
    }
    return true;
}

bool vbe_init(u32 width, u32 height) {
    if (vbe_read_reg(VBE_DISPI_INDEX_ID) < VBE_DISPI_ID_MIN) {
        return false;
    }

    u8 bus, device, function;
    if (!find_vga_device(&bus, &device, &function)) {
        return false;
    }
    u32 lfb_phys = pci_read_bar0(bus, device, function);
    if (lfb_phys == 0) {
        return false;
    }

    vbe_write_reg(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
    vbe_write_reg(VBE_DISPI_INDEX_XRES, (u16) width);
    vbe_write_reg(VBE_DISPI_INDEX_YRES, (u16) height);
    vbe_write_reg(VBE_DISPI_INDEX_BPP, 32);
    vbe_write_reg(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

    u32 pitch = width * 4;
    u32 total_bytes = pitch * height;
    if (!map_framebuffer_pages((u64) lfb_phys, total_bytes)) {
        return false;
    }

    g_fb_lfb_phys = lfb_phys;
    g_fb_vaddr = FB_VADDR;
    g_fb_width = width;
    g_fb_height = height;
    g_fb_pitch = pitch;
    g_fb_enabled = true;
    device_manager_register("Bochs VBE Framebuffer", DEVICE_CATEGORY_PLATFORM, 0);
    return true;
}

// Real linear framebuffer already negotiated by GRUB/QEMU's own
// multiboot loader via VESA/GOP before the kernel ran (boot.s's MB_FLAGS
// bit2 request) - works on any real GPU, since the bootloader/firmware
// did the mode-setting, not this driver. Reads back whatever was
// actually granted (which may differ from the 800x600/32bpp requested)
// rather than assuming it.
bool vbe_init_multiboot(void) {
    if (g_multiboot_info_ptr == 0) {
        return false;
    }
    if (g_multiboot_magic == MULTIBOOT2_BOOTLOADER_MAGIC) {
        // Real safety gate, not redundant: if we actually booted via
        // multiboot2, g_multiboot_info_ptr points at a completely
        // different tag-list structure (see vbe_init_multiboot2()
        // below) - reading it as the multiboot1 flat struct here would
        // interpret unrelated memory as "flags"/"framebuffer_*", the
        // same false-positive-read bug class already found once for
        // real Bochs-DISPI hardware detection (project_vbe_real_hardware_bug).
        return false;
    }
    multiboot_info* info = (multiboot_info*) ((u64) g_multiboot_info_ptr);
    if ((info->flags & MULTIBOOT_INFO_FLAG_FRAMEBUFFER) == 0) {
        return false;
    }
    if (info->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
        return false;
    }
    if (info->framebuffer_bpp != 32) {
        return false;
    }
    if (info->framebuffer_addr == 0) {
        return false;
    }

    u32 pitch = info->framebuffer_pitch;
    u32 height = info->framebuffer_height;
    u32 total_bytes = pitch * height;
    if (!map_framebuffer_pages(info->framebuffer_addr, total_bytes)) {
        return false;
    }

    g_fb_lfb_phys = (u32) info->framebuffer_addr;
    g_fb_vaddr = FB_VADDR;
    g_fb_width = info->framebuffer_width;
    g_fb_height = height;
    g_fb_pitch = pitch;
    g_fb_enabled = true;
    device_manager_register("Multiboot Framebuffer", DEVICE_CATEGORY_PLATFORM, 0);
    return true;
}

// Real multiboot2 framebuffer path - the actual fix for real UEFI
// hardware (multiboot1's own video-mode request hard-failed boot under
// this dev laptop's UEFI GRUB, see reference_multiboot1_uefi_video_limitation
// / boot.s's own comment). Walks the real multiboot2 tag list (shared
// walker, frames.h's multiboot2_next_tag()) looking for the
// framebuffer info tag - same honest "graceful false, not assumed"
// validation vbe_init_multiboot() already established for multiboot1.
bool vbe_init_multiboot2(void) {
    if (g_multiboot_magic != MULTIBOOT2_BOOTLOADER_MAGIC || g_multiboot_info_ptr == 0) {
        return false;
    }
    multiboot2_info* mb2 = (multiboot2_info*) ((u64) g_multiboot_info_ptr);
    multiboot2_tag* tag = (multiboot2_tag*) ((u64) mb2 + sizeof(multiboot2_info));
    while (tag->type != MULTIBOOT2_TAG_TYPE_END) {
        if (tag->type == MULTIBOOT2_TAG_TYPE_FRAMEBUFFER) {
            multiboot2_tag_framebuffer* fb = (multiboot2_tag_framebuffer*) tag;
            if (fb->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB) {
                return false;
            }
            if (fb->framebuffer_bpp != 32) {
                return false;
            }
            if (fb->framebuffer_addr == 0) {
                return false;
            }
            u32 pitch = fb->framebuffer_pitch;
            u32 height = fb->framebuffer_height;
            u32 total_bytes = pitch * height;
            if (!map_framebuffer_pages(fb->framebuffer_addr, total_bytes)) {
                return false;
            }
            g_fb_lfb_phys = (u32) fb->framebuffer_addr;
            g_fb_vaddr = FB_VADDR;
            g_fb_width = fb->framebuffer_width;
            g_fb_height = height;
            g_fb_pitch = pitch;
            g_fb_enabled = true;
            device_manager_register("Multiboot2 Framebuffer", DEVICE_CATEGORY_PLATFORM, 0);
            return true;
        }
        tag = multiboot2_next_tag(tag);
    }
    return false;
}

bool graphics_init(u32 preferred_width, u32 preferred_height) {
    if (vbe_init_multiboot2()) {
        return true;
    }
    if (vbe_init_multiboot()) {
        return true;
    }
    return vbe_init(preferred_width, preferred_height);
}

void fb_put_pixel(u32 x, u32 y, u32 color) {
    if (!g_fb_enabled || x >= g_fb_width || y >= g_fb_height) {
        return;
    }
    volatile u32* pixel = (volatile u32*) (g_fb_vaddr + (u64) y * g_fb_pitch + (u64) x * 4);
    *pixel = color;
}

u32 fb_get_pixel(u32 x, u32 y) {
    if (!g_fb_enabled || x >= g_fb_width || y >= g_fb_height) {
        return 0;
    }
    volatile u32* pixel = (volatile u32*) (g_fb_vaddr + (u64) y * g_fb_pitch + (u64) x * 4);
    return *pixel;
}

void fb_fill_rect(u32 x, u32 y, u32 w, u32 h, u32 color) {
    u32 row = 0;
    while (row < h) {
        u32 col = 0;
        while (col < w) {
            fb_put_pixel(x + col, y + row, color);
            col = col + 1;
        }
        row = row + 1;
    }
}

void fb_draw_char(u32 x, u32 y, char c, u32 fg, u32 bg) {
    u8 rows[FONT_GLYPH_HEIGHT];
    if (!font_get_glyph(c, rows)) {
        return;  // unsupported character - leave the cell untouched
    }
    u32 row = 0;
    while (row < FONT_GLYPH_HEIGHT) {
        u32 col = 0;
        while (col < FONT_GLYPH_WIDTH) {
            bool on = (rows[row] >> (FONT_GLYPH_WIDTH - 1 - col)) & 1;
            fb_put_pixel(x + col, y + row, on ? fg : bg);
            col = col + 1;
        }
        row = row + 1;
    }
}

// Single line only, no wrapping - a known limitation, closed in a later
// milestone alongside the font's own limited character set.
void fb_draw_string(u32 x, u32 y, const char* s, u32 fg, u32 bg) {
    u32 cursor = x;
    int i = 0;
    while (s[i] != '\0') {
        fb_draw_char(cursor, y, s[i], fg, bg);
        cursor = cursor + FONT_GLYPH_WIDTH + 1;
        i = i + 1;
    }
}
