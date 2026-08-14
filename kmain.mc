// The MiniC side of the kernel. By the time boot.s's `call _start` reaches
// here, we're in 64-bit long mode with a real stack and the first 1GB of
// physical memory identity-mapped (paging is on, but virtual == physical
// for everything this touches) - including the VGA text buffer at
// 0xB8000, which needs no driver at all: it's just memory.

struct VgaChar {
    u8 character;
    u8 color;
}

u8 gSerialByte;

// The one-byte port write raw asm(...) can't take as a MiniC argument (no
// operand binding) - route it through a global the same way asm_demo.mc
// does. `out`'s immediate-port form only reaches ports 0-255, so 0x3F8
// (COM1) needs the dx-indirect form.
void serialPutc(u8 c) {
    gSerialByte = c;
    asm("mov al, [rip+gSerialByte]\nmov dx, 0x3F8\nout dx, al");
}

void serialPrint(char* s) {
    int i = 0;
    while (s[i] != (char) 0) {
        serialPutc((u8) s[i]);
        i = i + 1;
    }
}

void _start() {
    volatile VgaChar* vga = (volatile VgaChar*) 0xB8000;
    char* message = "Hello from a MiniC kernel!";
    u8 color = 0x0F;   // white on black

    int i = 0;
    while (message[i] != (char) 0) {
        vga[i].character = (u8) message[i];
        vga[i].color = color;
        i = i + 1;
    }

    // Mirrors the same message to the serial port (COM1) - not part of
    // the milestone itself, just a way to verify the kernel actually
    // booted and ran without needing to look at a screen.
    serialPrint("Hello from a MiniC kernel!\n");

    while (true) {
        asm("hlt");
    }
}
