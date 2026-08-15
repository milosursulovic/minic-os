// Low-level output: the VGA text buffer, the serial port, and the raw
// port I/O (`in`/`out`) both are built on. asm(...) has to do the actual
// `in`/`out` instructions directly - there's no operand binding to hand
// it a MiniC value - so outb/inb relay through a global (same trick as
// boot.s's gMultibootInfoPtr) and everything else (serial, VGA, and
// later the PIC/PIT/keyboard) is ordinary MiniC built on top of them.

struct VgaChar {
    u8 character;
    u8 color;
}

volatile VgaChar* gVga;
int gVgaCursor;

u16 gOutPort;
u8 gOutByte;
u16 gInPort;
u8 gInByte;

void outb(u16 port, u8 value) {
    gOutPort = port;
    gOutByte = value;
    asm("mov dx, [rip+gOutPort]\nmov al, [rip+gOutByte]\nout dx, al");
}

u8 inb(u16 port) {
    gInPort = port;
    asm("mov dx, [rip+gInPort]\nin al, dx\nmov [rip+gInByte], al");
    return gInByte;
}

void serialPutc(u8 c) {
    outb(0x3F8, c);
}

void serialPrint(char* s) {
    int i = 0;
    while (s[i] != '\0') {
        serialPutc(s[i]);
        i = i + 1;
    }
}

void vgaPutc(char c) {
    gVga[gVgaCursor].character = c;
    gVga[gVgaCursor].color = 0x0F;
    gVgaCursor = gVgaCursor + 1;
}

void vgaPrint(char* s) {
    int i = 0;
    while (s[i] != '\0') {
        vgaPutc(s[i]);
        i = i + 1;
    }
}
