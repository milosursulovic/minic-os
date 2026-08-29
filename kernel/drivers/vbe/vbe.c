// Bochs/QEMU VBE "DISPI" extension: sets a linear framebuffer mode via
// plain port I/O, no VESA BIOS calls needed (real/unreal mode only, and
// we're already in long mode by the time any driver runs). The LFB's
// physical base comes from the VGA-class PCI device's own BAR0 - same
// discover-then-map_page() pattern net/e1000.c already established for
// its MMIO registers.

#include "vbe.h"
#include "../io/io.h"
#include "../pci/pci.h"
#include "../../mm/paging/paging.h"
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

// Below this, port 0x1CE/0x1CF isn't a real DISPI interface.
static const u16 VBE_DISPI_ID_MIN = 0xB0C0;

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
        if (g_pci_devices[i].class_code == 0x03) {
            *bus_out = g_pci_devices[i].bus;
            *device_out = g_pci_devices[i].device;
            *function_out = g_pci_devices[i].function;
            return true;
        }
        i = i + 1;
    }
    return false;
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
    u32 pages = (total_bytes + 4095) / 4096;

    u32 page = 0;
    while (page < pages) {
        u64 vaddr = FB_VADDR + ((u64) page * 4096);
        u64 paddr = ((u64) lfb_phys) + ((u64) page * 4096);
        if (!map_page(vaddr, paddr, 0x02 | PAGE_NX)) {  // framebuffer: writable, non-exec
            return false;
        }
        page = page + 1;
    }

    g_fb_lfb_phys = lfb_phys;
    g_fb_vaddr = FB_VADDR;
    g_fb_width = width;
    g_fb_height = height;
    g_fb_pitch = pitch;
    g_fb_enabled = true;
    return true;
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
