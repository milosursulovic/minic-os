# MiniC kernel

The start of the OS kernel phase: a multiboot1-compliant kernel image,
boots to 64-bit long mode, and writes directly to the VGA text buffer -
all real, all verified running in QEMU (byte-for-byte checked via the
QEMU monitor, not just "it didn't crash").

## Why there's hand-written assembly here

Multiboot drops you in 32-bit protected mode; this compiler only targets
x86-64. Getting from one to the other - loading a GDT, building page
tables, enabling long mode, far-jumping into a 64-bit code segment - is
program *structure*, below what MiniC's `asm("...")` (a function-body
statement, no operand binding) can express. Every kernel project hand-
writes an equivalent of this, even ones written in Rust or Zig.

- **`boot.s`** - the multiboot header, the 32-to-64-bit transition, and
  nothing else. Written directly against the real hardware, not through
  MiniC.
- **`kmain.mc`** - everything past that point, in ordinary MiniC. Writes
  a message to the VGA buffer (`0xB8000`, no driver needed - it's just
  memory) and mirrors it to the serial port for headless verification.
  Uses nothing beyond what the freestanding/systems phase already built:
  `volatile`, a plain `struct`, pointer indexing, `asm(...)` for the one
  raw `out` port write MiniC has no other way to express.
- **`linker.ld`** - places the multiboot header + code at the
  conventional 1MB load address multiboot expects.

## Building and running

Needs `qemu-system-x86_64` (`sudo apt install qemu-system-x86` on
Debian/Ubuntu/WSL) and a Linux-built `minicc` (`../build-linux/minicc`).

```bash
./build.sh          # assembles boot.s, compiles+assembles kmain.mc, links kernel.elf
./build.sh run       # also boots it in QEMU (curses display, in-terminal)
```

`build.sh` assembles both files as **32-bit ELF objects** even though
`kmain.mc`'s code (and `boot.s`'s post-transition half) runs in 64-bit
long mode - multiboot1's loader (and QEMU's/GRUB's implementation of it)
only understands a 32-bit ELF *container*. `.code32`/`.code64` are
per-region encoding directives, independent of that container format, so
minicc's output gets a `.code64` directive prepended before assembly (it
only ever targets hosted 64-bit ELF on its own, so it doesn't emit one
itself).

To check output without a display, redirect the serial port to a file:

```bash
qemu-system-x86_64 -kernel kernel.elf -display none -serial file:serial.log -no-reboot
```

## Known limitations (milestone 1 - on purpose, for now)

- No interrupts, no paging beyond the flat identity map covering the
  first 1GB, no memory allocator, no keyboard/other input - literally
  "boot and print," the smallest real slice of a kernel.
- x86-64/multiboot1/QEMU only - no real-hardware boot testing, no
  Multiboot2/GRUB ISO packaging yet (multiboot1 + QEMU's `-kernel` was
  chosen for milestone 1 specifically because it needs no bootloader
  tooling at all - just QEMU).
- The identity-mapped 1GB region is built with 2MB pages sized generously
  around what's actually needed (kernel at 1MB, VGA buffer at ~736KB) -
  not yet a real page-table layout a kernel would keep long-term.
