// Low-level output: the VGA text buffer, the serial port, and the raw
// port I/O (`in`/`out`) both are built on. asm(...) has to do the actual
// `in`/`out` instructions directly - there's no operand binding to hand
// it a MiniC value - so outb/inb relay through a global (same trick as
// boot.s's g_multiboot_info_ptr) and everything else (serial, VGA, and
// later the PIC/PIT/keyboard) is ordinary MiniC built on top of them.

struct vga_char {
    u8 character;
    u8 color;
}

volatile vga_char* g_vga;
int g_vga_cursor;

u16 g_out_port;
u8 g_out_byte;
u16 g_in_port;
u8 g_in_byte;

void outb(u16 port, u8 value) {
    g_out_port = port;
    g_out_byte = value;
    asm("mov dx, [rip+g_out_port]\nmov al, [rip+g_out_byte]\nout dx, al");
}

u8 inb(u16 port) {
    g_in_port = port;
    asm("mov dx, [rip+g_in_port]\nin al, dx\nmov [rip+g_in_byte], al");
    return g_in_byte;
}

u16 g_out_word;
u16 g_in_word;

// 16-bit port I/O - milestone 16's ATA PIO driver transfers a sector's
// bytes two at a time through the data port, not one at a time like
// every earlier port-I/O user (VGA/keyboard/PIT) needed.
void outw(u16 port, u16 value) {
    g_out_port = port;
    g_out_word = value;
    asm("mov dx, [rip+g_out_port]\nmov ax, [rip+g_out_word]\nout dx, ax");
}

u16 inw(u16 port) {
    g_in_port = port;
    asm("mov dx, [rip+g_in_port]\nin ax, dx\nmov [rip+g_in_word], ax");
    return g_in_word;
}

u32 g_out_dword;
u32 g_in_dword;

// 32-bit port I/O - milestone 29's PCI config space access is the first
// user: the legacy CONFIG_ADDRESS/CONFIG_DATA mechanism (ports 0xCF8/
// 0xCFC) is defined in terms of whole 32-bit dwords, not bytes or words.
void outl(u16 port, u32 value) {
    g_out_port = port;
    g_out_dword = value;
    asm("mov dx, [rip+g_out_port]\nmov eax, [rip+g_out_dword]\nout dx, eax");
}

u32 inl(u16 port) {
    g_in_port = port;
    asm("mov dx, [rip+g_in_port]\nin eax, dx\nmov [rip+g_in_dword], eax");
    return g_in_dword;
}

void serial_putc(u8 c) {
    outb(0x3F8, c);
}

void serial_print(char* s) {
    int i = 0;
    while (s[i] != '\0') {
        serial_putc(s[i]);
        i = i + 1;
    }
}

void vga_putc(char c) {
    g_vga[g_vga_cursor].character = c;
    g_vga[g_vga_cursor].color = 0x0F;
    g_vga_cursor = g_vga_cursor + 1;
}

void vga_print(char* s) {
    int i = 0;
    while (s[i] != '\0') {
        vga_putc(s[i]);
        i = i + 1;
    }
}
