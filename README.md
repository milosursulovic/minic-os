# MiniC kernel

The start of the OS kernel phase: a multiboot1-compliant kernel image
that boots to 64-bit long mode, handles real interrupts (timer +
keyboard), has a real heap (`kalloc`/`kfree`, splitting and two-way
coalescing) and a physical frame allocator driven by the multiboot memory
map, and runs a minimal interactive shell over VGA - all real, all
verified running in QEMU (byte-for-byte checked via the QEMU monitor's
memory dump and `sendkey`, not just "it didn't crash").

## Why there's hand-written assembly here

Multiboot drops you in 32-bit protected mode; this compiler only targets
x86-64. Getting from one to the other - loading a GDT, building page
tables, enabling long mode, far-jumping into a 64-bit code segment - is
program *structure*, below what MiniC's `asm("...")` (a function-body
statement, no operand binding) can express. Every kernel project hand-
writes an equivalent of this, even ones written in Rust or Zig. Same
reasoning covers interrupt entry: saving/restoring full register state
and normalizing "sometimes the CPU pushes an error code, sometimes it
doesn't" into one common call is calling-convention plumbing, not
something a MiniC function body can do to itself.

- **`boot.s`** - the multiboot header and the 32-to-64-bit transition,
  plus stashing EBX (multiboot's pointer to its info structure, handed to
  the kernel at entry and never overwritten since) into a MiniC global
  before calling `_start` - `_start` takes no parameters, same global-
  relay trick as `outb`'s port/value. Written directly against the real
  hardware, not through MiniC.
- **`interrupts.s`** - entry stubs for the exceptions/IRQs the kernel
  handles (divide-by-zero, GPF, page fault, timer, keyboard): save every
  register, call into MiniC with the vector number + error code, restore,
  `iretq`. Same "below what `asm(...)` can express" reasoning as boot.s.
- **`kmain.mc`** - everything past "here's the vector number," in
  ordinary MiniC: the IDT (an array of `packed struct` entries), 8259 PIC
  remapping, PIT reconfiguration, the timer/keyboard handlers, a real
  free-list heap (`kalloc`/`kfree` - splits blocks on alloc, coalesces
  adjacent free blocks both forward *and* backward on free), a physical
  frame bitmap allocator (`allocFrame`/`freeFrame`) built from the
  multiboot memory map (`MultibootInfo`/`MmapEntry`, both `packed struct` -
  `MmapEntry` in particular has a genuinely unaligned field by the real
  spec, exactly the case `packed` exists for), a minimal interactive shell
  (`help`/`clear`/`ticks`/`alloc`/`free`/`free <addr>`/`mem`/`reset`/
  `frame`/`frames`/`echo <text>`, built on the keyboard handler's line
  buffer), and the VGA/serial output. Uses nothing beyond what the
  freestanding/systems phase already built - `volatile`, `packed struct`,
  pointer indexing, `asm(...)` for the handful of raw port I/O
  instructions (`out`/`in`/`lidt`/`sti`) MiniC has no other way to
  express.
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

With a real display (`./build.sh run`), type at the shell prompt (`>` on
the second row) - lowercase letters, digits, space, and enter all work:

```
> alloc
allocated 64 bytes at 0x10e149
> alloc
allocated 64 bytes at 0x10e199
> free 0x10e149
freed 0x10e149
> free
freed 0x10e199
> mem
free: 0xffff0
> frames
free frames: 0x7be0 / 0x40000
> echo hello world
hello world
```

That `mem` result is only possible with *both* directions of coalescing
working: freeing the first block, then the second, then having them
(plus the trailing free space) merge back into exactly one block again -
recovering the header overhead a partial merge would have left behind.

## Known limitations (on purpose, for now)

- Only 5 interrupt vectors are wired up: divide-by-zero (0), GPF (13),
  page fault (14), timer (32), keyboard (33). Any other exception hits an
  empty IDT entry and triple-faults (QEMU resets) - the same
  `ISR_NOERR`/`ISR_ERR` macro pattern in `interrupts.s` covers the rest
  of 0-31 when something actually needs them.
- Backward coalescing rescans the whole arena from the start to find the
  preceding block (no back-pointer) - O(n) per `free`, fine for a hobby
  heap, not a real allocator's profile.
- The frame allocator reserves the first 4MB of physical memory
  unconditionally rather than computing exactly where the kernel image/
  heap arena/frame bitmap end - simpler, and there's plenty of room to
  spare, but wastes a few MB of tracking granularity.
- The frame allocator and the heap are two separate, unconnected systems:
  frames aren't yet used to back anything (no dynamic page-table
  extension), and the heap's 1MB arena is still a fixed `.bss` reservation
  rather than frame-backed. Real paging beyond the flat 1GB identity map
  needs the frame allocator this milestone built, but doesn't use it yet.
- No scheduler/multitasking - one linear `_start` plus whatever the
  timer/keyboard handlers do.
- Keyboard support is lowercase letters, digits, space, and enter only (a
  small hand-built scancode table in `kmain.mc`), scancode set 1, no
  shift/modifier handling, no scrolling once the VGA cursor runs
  off-screen.
- x86-64/multiboot1/QEMU only - no real-hardware boot testing, no
  Multiboot2/GRUB ISO packaging yet (multiboot1 + QEMU's `-kernel` was
  chosen specifically because it needs no bootloader tooling at all -
  just QEMU).
- The identity-mapped 1GB region is built with 2MB pages sized generously
  around what's actually needed (kernel at 1MB, VGA buffer at ~736KB) -
  not yet a real page-table layout a kernel would keep long-term.
- Interrupt handlers don't save/restore SSE/XMM state - fine today since
  nothing running at interrupt time uses floats, a real gap once
  something does.
