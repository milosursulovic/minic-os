// Driver for the Intel e1000 (82540EM) gigabit NIC at PCI 0:3.0. Unlike
// every earlier driver, its registers are memory-mapped (MMIO), not port
// I/O: BAR0 gives a physical address that must be map_page()'d before use.

#include "e1000.h"
#include "../drivers/pci.h"
#include "../mm/paging.h"
#include "../mm/frames.h"

static const u8 E1000_BUS = 0;
static const u8 E1000_DEVICE = 3;
static const u8 E1000_FUNCTION = 0;

static const u32 E1000_REG_STATUS = 0x0008;
static const u32 E1000_REG_RAL0 = 0x5400;
static const u32 E1000_REG_RAH0 = 0x5404;

// STATUS register bit 1 = Link Up.
static const u32 E1000_STATUS_LU = 0x2;

static const u64 E1000_MMIO_VADDR = 0x60000000;
static const u64 E1000_MMIO_PAGES = 8;

static u64 g_e1000_mmio_base;

u32 e1000_read_reg(u32 offset) {
    volatile u32* reg = (volatile u32*) (g_e1000_mmio_base + (u64) offset);
    return *reg;
}

void e1000_write_reg(u32 offset, u32 value) {
    volatile u32* reg = (volatile u32*) (g_e1000_mmio_base + (u64) offset);
    *reg = value;
}

static const u8 PCI_REG_COMMAND = 0x04;
static const u32 PCI_COMMAND_MEMORY_SPACE = 0x2;
static const u32 PCI_COMMAND_BUS_MASTER = 0x4;

// Returns false rather than mapping garbage if the expected device isn't present.
bool e1000_init(void) {
    u16 vendor_id = pci_config_read_word(E1000_BUS, E1000_DEVICE, E1000_FUNCTION, 0x00);
    u16 device_id = pci_config_read_word(E1000_BUS, E1000_DEVICE, E1000_FUNCTION, 0x02);
    if (vendor_id != 0x8086 || device_id != 0x100e) {
        return false;
    }

    u32 command = pci_config_read_dword(E1000_BUS, E1000_DEVICE, E1000_FUNCTION, PCI_REG_COMMAND);
    command = command | PCI_COMMAND_MEMORY_SPACE | PCI_COMMAND_BUS_MASTER;
    pci_config_write_dword(E1000_BUS, E1000_DEVICE, E1000_FUNCTION, PCI_REG_COMMAND, command);

    u32 mmio_phys = pci_read_bar0(E1000_BUS, E1000_DEVICE, E1000_FUNCTION);
    if (mmio_phys == 0) {
        return false;
    }

    u64 page = 0;
    while (page < E1000_MMIO_PAGES) {
        u64 vaddr = E1000_MMIO_VADDR + (page * 4096);
        u64 paddr = ((u64) mmio_phys) + (page * 4096);
        if (!map_page(vaddr, paddr, 0x02 | PAGE_NX)) {  // device regs: writable, non-exec, kernel-only
            return false;
        }
        page = page + 1;
    }
    g_e1000_mmio_base = E1000_MMIO_VADDR;
    return true;
}

// RAL0/RAH0 are pre-loaded by the (emulated) EEPROM at power-on.
void e1000_get_mac(u8* mac_out) {
    u32 low = e1000_read_reg(E1000_REG_RAL0);
    u32 high = e1000_read_reg(E1000_REG_RAH0);
    mac_out[0] = (u8) (low & 0xFF);
    mac_out[1] = (u8) ((low >> 8) & 0xFF);
    mac_out[2] = (u8) ((low >> 16) & 0xFF);
    mac_out[3] = (u8) ((low >> 24) & 0xFF);
    mac_out[4] = (u8) (high & 0xFF);
    mac_out[5] = (u8) ((high >> 8) & 0xFF);
}

bool e1000_link_up(void) {
    u32 status = e1000_read_reg(E1000_REG_STATUS);
    return (status & E1000_STATUS_LU) != 0;
}

// TX/RX descriptor rings: the driver hands the hardware pointers to buffers
// in RAM, and it DMAs to/from them directly. packed matches hardware's 16-byte layout.
typedef struct __attribute__((packed)) {
    u64 buffer_addr;
    u16 length;
    u8 cso;
    u8 cmd;
    u8 status;
    u8 css;
    u16 special;
} tx_descriptor;

typedef struct __attribute__((packed)) {
    u64 buffer_addr;
    u16 length;
    u16 checksum;
    u8 status;
    u8 errors;
    u16 special;
} rx_descriptor;

static const u32 E1000_REG_TCTL = 0x0400;
static const u32 E1000_REG_TIPG = 0x0410;
static const u32 E1000_REG_TDBAL = 0x3800;
static const u32 E1000_REG_TDBAH = 0x3804;
static const u32 E1000_REG_TDLEN = 0x3808;
static const u32 E1000_REG_TDH = 0x3810;
static const u32 E1000_REG_TDT = 0x3818;

static const u32 E1000_REG_RCTL = 0x0100;
static const u32 E1000_REG_RDBAL = 0x2800;
static const u32 E1000_REG_RDBAH = 0x2804;
static const u32 E1000_REG_RDLEN = 0x2808;
static const u32 E1000_REG_RDH = 0x2810;
static const u32 E1000_REG_RDT = 0x2818;

// RS (report status) must be set or the hardware never sets DD and polling spins forever.
static const u8 TX_CMD_EOP = 0x01;
static const u8 TX_CMD_IFCS = 0x02;
static const u8 TX_CMD_RS = 0x08;
static const u8 TX_STATUS_DD = 0x01;

// Standard full-duplex config values from Intel's datasheet.
static const u32 E1000_TCTL_VALUE = 0x014000FA;
static const u32 E1000_TIPG_VALUE = 0x0060200A;
static const u32 E1000_RCTL_VALUE = 0x0400800A;

static tx_descriptor* g_tx_ring;
static u32 g_tx_tail;
static u8* g_tx_buffer;

static rx_descriptor* g_rx_ring;
static u8* g_rx_buffers[RX_RING_SIZE];

// Must run after e1000_init() - needs MMIO already mapped and bus-mastering enabled.
bool e1000_init_rings(void) {
    void* tx_ring_frame = alloc_frame();
    void* tx_buf_frame = alloc_frame();
    if (tx_ring_frame == NULL || tx_buf_frame == NULL) {
        return false;
    }
    g_tx_ring = (tx_descriptor*) tx_ring_frame;
    g_tx_buffer = (u8*) tx_buf_frame;
    u32 i = 0;
    while (i < TX_RING_SIZE) {
        g_tx_ring[i].buffer_addr = 0;
        g_tx_ring[i].length = 0;
        g_tx_ring[i].cmd = 0;
        g_tx_ring[i].status = TX_STATUS_DD;   // every unused slot starts "done"
        i = i + 1;
    }
    g_tx_tail = 0;

    e1000_write_reg(E1000_REG_TDBAL, (u32) ((u64) tx_ring_frame));
    e1000_write_reg(E1000_REG_TDBAH, 0);
    e1000_write_reg(E1000_REG_TDLEN, TX_RING_SIZE * 16);
    e1000_write_reg(E1000_REG_TDH, 0);
    e1000_write_reg(E1000_REG_TDT, 0);
    e1000_write_reg(E1000_REG_TIPG, E1000_TIPG_VALUE);
    e1000_write_reg(E1000_REG_TCTL, E1000_TCTL_VALUE);

    void* rx_ring_frame = alloc_frame();
    if (rx_ring_frame == NULL) {
        return false;
    }
    g_rx_ring = (rx_descriptor*) rx_ring_frame;
    i = 0;
    while (i < RX_RING_SIZE) {
        void* buf = alloc_frame();
        if (buf == NULL) {
            return false;
        }
        g_rx_buffers[i] = (u8*) buf;
        g_rx_ring[i].buffer_addr = (u64) buf;
        g_rx_ring[i].length = 0;
        g_rx_ring[i].status = 0;
        i = i + 1;
    }

    e1000_write_reg(E1000_REG_RDBAL, (u32) ((u64) rx_ring_frame));
    e1000_write_reg(E1000_REG_RDBAH, 0);
    e1000_write_reg(E1000_REG_RDLEN, RX_RING_SIZE * 16);
    e1000_write_reg(E1000_REG_RDH, 0);
    e1000_write_reg(E1000_REG_RDT, RX_RING_SIZE - 1);  // all descriptors available to hardware
    e1000_write_reg(E1000_REG_RCTL, E1000_RCTL_VALUE);

    return true;
}

// Polls (bounded) for the descriptor's DD bit, the hardware's own completion signal.
bool e1000_send(u8* data, u16 len) {
    u32 i = 0;
    while (i < (u32) len) {
        g_tx_buffer[i] = data[i];
        i = i + 1;
    }

    u32 slot = g_tx_tail;
    g_tx_ring[slot].buffer_addr = (u64) g_tx_buffer;
    g_tx_ring[slot].length = len;
    g_tx_ring[slot].cmd = TX_CMD_EOP | TX_CMD_IFCS | TX_CMD_RS;
    g_tx_ring[slot].status = 0;

    g_tx_tail = (slot + 1) % TX_RING_SIZE;
    e1000_write_reg(E1000_REG_TDT, g_tx_tail);

    u32 spins = 0;
    while (spins < 1000000) {
        if ((g_tx_ring[slot].status & TX_STATUS_DD) != 0) {
            return true;
        }
        spins = spins + 1;
    }
    return false;
}

// Short spin bound per call, since a real external reply takes wall-clock time
// a tight instruction spin can't reliably provide. Callers wanting to wait for
// a real reply should call this repeatedly against a tick-based timeout instead.
static u32 g_rx_head;

u16 e1000_receive(u8* out, u16 max_len) {
    u32 spins = 0;
    while (spins < 20000) {
        if ((g_rx_ring[g_rx_head].status & 0x01) != 0) {
            u16 len = g_rx_ring[g_rx_head].length;
            u16 copy_len = len;
            if (copy_len > max_len) {
                copy_len = max_len;
            }
            u16 i = 0;
            while (i < copy_len) {
                out[i] = g_rx_buffers[g_rx_head][i];
                i = i + 1;
            }
            g_rx_ring[g_rx_head].status = 0;
            e1000_write_reg(E1000_REG_RDT, g_rx_head);
            g_rx_head = (g_rx_head + 1) % RX_RING_SIZE;
            return len;
        }
        spins = spins + 1;
    }
    return 0;
}
