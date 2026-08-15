# MiniC kernel

The start of the OS kernel phase: a multiboot1-compliant kernel image
that boots to 64-bit long mode, handles real interrupts (timer +
keyboard), has a real heap (`kalloc`/`kfree`, splitting and two-way
coalescing, growing on demand by mapping fresh physical frames rather
than a fixed arena), a physical frame allocator driven by the multiboot
memory map, real dynamic paging (walking/creating PML4/PDPT/PD/PT chains
to map memory anywhere, not just inside the boot-time static identity
map), a cooperative scheduler switching between real kernel tasks on
their own stacks, and runs a minimal interactive shell over VGA - all
real, all verified running in QEMU (byte-for-byte checked via the QEMU
monitor's memory dump and `sendkey`, not just "it didn't crash").

Writing this surfaced six real MiniC language gaps (char literals, `char`
arithmetic/comparisons, `sizeof`'s int-width flexibility, `break`/
`continue`, array-to-`void*` decay, `extern` globals) - all fixed as real
compiler features rather than left as workarounds; see the
[minic repo's roadmap](https://github.com/milosursulovic/minic#roadmap)
for the writeup. `kmain.mc`
uses all of them directly now (`'a'`, `break`, `c - '0'`, etc.), not the
numeric-ASCII-code/boolean-flag workarounds it launched with.

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
something a MiniC function body can do to itself. A context switch is
the same kind of gap: MiniC never keeps a value live in a register
across a statement boundary, but switching tasks means preserving
register state *across* what looks like one ordinary call into a
completely different task's stack.

## Project layout

One folder per subsystem, with room to split further as each one grows
(e.g. `shell/` gaining one file per command, `isr/` gaining one per
vector) rather than everything staying flat in a single file:

```
kmain.mc          entry point (_start) + the imports wiring everything together
boot/             hand-written assembly - below what asm(...) can express
  boot.s            multiboot header, 32-to-64-bit transition
  interrupts.s       ISR/IRQ entry stubs (save/restore, call into MiniC)
  linker.ld          places the multiboot header + code at the 1MB load address
drivers/          hardware setup and I/O
  io.mc              VGA text buffer, serial port, raw in/out port I/O
  interrupts_init.mc IDT + 8259 PIC remap + PIT reconfiguration
  keyboard.mc        scancode table + the shell's line buffer
mm/               memory management
  heap.mc            kalloc/kfree free-list allocator, grows on demand via mm/paging.mc
  frames.mc          multiboot memory map parser + physical frame bitmap allocator
  paging.mc          dynamic PML4/PDPT/PD/PT paging
lib/              no-libc helpers
  strings.mc         streq/startsWith/parseHex/printHex
isr/              interrupt dispatch
  isr.mc             interrupt_handler, called from interrupts.s's stubs
sched/            cooperative task scheduler
  switch.s           hand-written context switch (below what asm(...) can express)
  task.mc            Task table, createTask/yield, two demo tasks
shell/            the interactive shell
  shell.mc           cmd* functions + runCommand dispatch
```

- **`boot/boot.s`** - the multiboot header and the 32-to-64-bit
  transition, plus stashing EBX (multiboot's pointer to its info
  structure, handed to the kernel at entry and never overwritten since)
  into a MiniC global before calling `_start` - `_start` takes no
  parameters, same global-relay trick as `outb`'s port/value. Written
  directly against the real hardware, not through MiniC.
- **`boot/interrupts.s`** - entry stubs for the exceptions/IRQs the
  kernel handles (divide-by-zero, GPF, page fault, timer, keyboard): save
  every register, call into MiniC with the vector number + error code,
  restore, `iretq`. Same "below what `asm(...)` can express" reasoning as
  boot.s.
- **`sched/switch.s`** - `switch_context(oldRspOut, newRsp)`: pushes the
  current task's callee-saved registers, stashes the resulting stack
  pointer, switches to the next task's stack, pops its callee-saved
  registers, `ret`s. Same "below `asm(...)`" reasoning again - a context
  switch's entire point is preserving register state across what looks
  like one ordinary call into a different task's stack.
- **`kmain.mc`** and its imports - everything past "here's the vector
  number," in ordinary MiniC. `kmain.mc` itself is just the entry point
  (`_start`) plus an `import` of every module above, using nothing beyond
  what the freestanding/systems phase already built (`volatile`,
  `packed struct`, pointer indexing, `asm(...)` for the handful of raw
  instructions - `out`/`in`/`lidt`/`sti`/`invlpg`/reading `cr2`/`cr3` -
  MiniC has no other way to express). `mm/paging.mc`'s `mapPage` walks/
  creates a PML4->PDPT->PD->PT chain of 4KB pages, allocating fresh
  frames (from `mm/frames.mc`) for any missing table level - reads CR3
  once at boot into a MiniC global the same way `gMultibootInfoPtr`
  works, since every physical address it touches, table pages included,
  lands inside the boot-time flat 1GB identity map and so is directly
  dereferenceable as an ordinary pointer, no temporary-mapping trick
  needed. `mm/frames.mc`'s multiboot parser uses `MultibootInfo`/
  `MmapEntry`, both `packed struct` - `MmapEntry` in particular has a
  genuinely unaligned field by the real spec, exactly the case `packed`
  exists for. `mm/heap.mc` is what all of that unblocks: `kalloc`
  starts the heap at a 64KB mapping and grows it a chunk at a time
  (`allocFrame` + `mapPage`, at a dedicated virtual base well clear of
  both the static identity map and the `map` command's demo page) once
  the free list runs dry, up to a 16MB cap - not the fixed `.bss` arena
  earlier milestones used. `sched/task.mc` is a fixed table of kernel
  tasks, each with its own `kalloc`'d stack; `createTask()` hand-builds a
  new task's initial stack to look exactly like what `switch_context()`
  would have left behind, with the task's entry point as a fake "return
  address," so the first switch into it lands there via an ordinary
  `ret`. Cooperative, not preemptive yet - `yield()` is the only thing
  that switches tasks, round-robin. `shell/shell.mc` is the interactive
  shell (`help`/`clear`/`ticks`/`alloc`/`bigalloc`/`free`/`free <addr>`/
  `mem`/`reset`/`frame`/`unframe`/`frames`/`map`/`tasks`/`echo <text>`)
  built on `drivers/keyboard.mc`'s line buffer.
- **`boot/linker.ld`** - places the multiboot header + code at the
  conventional 1MB load address multiboot expects.

## Building and running

Needs `qemu-system-x86_64` (`sudo apt install qemu-system-x86` on
Debian/Ubuntu/WSL) and a Linux-built `minicc` from the
[minic](https://github.com/milosursulovic/minic) repo - `build.sh`
defaults to `../compiler/build-linux/minicc` (a sibling checkout of that
repo named `compiler/`), override with `MINICC=/path/to/minicc ./build.sh`
for any other layout.

```bash
./build.sh          # assembles boot.s, compiles+assembles kmain.mc, links kernel.elf
./build.sh run       # also boots it in QEMU (curses display, in-terminal)
./build.sh iso       # also packages a GRUB-bootable minic-os.iso
```

## Running outside QEMU (VirtualBox, VMware, real hardware)

QEMU's `-kernel kernel.elf` is a QEMU-only shortcut - it understands
multiboot1 itself and skips needing a real bootloader. Nothing else does
that, so anywhere else (VirtualBox, VMware, real hardware) needs an
actual bootloader in front of the same `kernel.elf`. Since the kernel is
already multiboot1-compliant, GRUB2 can chainload it directly - no
kernel-side changes needed, just packaging.

`./build.sh iso` needs `grub-mkrescue`, `xorriso`, and `mtools`
(`sudo apt install grub-pc-bin grub-common xorriso mtools` on
Debian/Ubuntu/WSL). It copies `kernel.elf` into `iso/boot/kernel.elf`
(gitignored - `iso/boot/grub/grub.cfg` is the only checked-in part of
that tree) and runs `grub-mkrescue -o minic-os.iso iso`, producing a
standard bootable CD image - verified booting byte-for-byte identically
to the direct QEMU path via `qemu-system-x86_64 -cdrom minic-os.iso`
(the same BIOS+GRUB route VirtualBox takes, unlike `-kernel`).

For VirtualBox specifically:

1. New VM - Type "Other", Version "Other/Unknown (64-bit)". No guest
   additions, no EFI (this kernel boots via legacy BIOS + GRUB).
2. Give it a small amount of RAM (128MB+ is plenty) and skip creating a
   virtual hard disk - this kernel doesn't touch one.
3. Settings → Storage → attach `minic-os.iso` as the optical drive.
4. Start the VM - GRUB's menu appears, boots straight into the kernel.

VMware and real hardware (via a USB stick written with `dd` or similar)
should work the same way, unverified so far.

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
allocated 64 bytes at 0x50008030
> alloc
allocated 64 bytes at 0x50008080
> free
freed 0x50008080
> mem
free: 0x7f80 / 0x10000
> frames
free frames: 0x7bce / 0x40000
> map
mapped 0x40000000 -> 0x412000, wrote/read 0xcafebabe
> tasks
task1: 0x5137 task2: 0x5137
> echo hello world
hello world
```

Those first two `alloc` addresses aren't right at the heap's base
address anymore, the way they were before milestone 8 - `sched/task.mc`'s
two demo tasks each `kalloc` a 16KB stack during boot, before the shell
even starts, so by the time a user types `alloc` the first ~32KB of the
heap's initial 64KB mapping is already spoken for.

That `map` result proves the whole dynamic-paging chain: a frame beyond
the multiboot memory map's low end, mapped at a virtual address a full
1GB past anything boot.s set up statically, written through and read
back correctly - real PML4/PDPT/PD/PT walking and on-demand table
creation, not just "didn't crash."

That `tasks` result - both counters exactly equal - is round-robin
cooperative scheduling working as designed: the main shell loop calls
`yield()` once per wake-up, which cascades through task1 and task2 (each
increments its counter once, then immediately yields onward) before
control returns to the shell, so all three "tasks" advance in lockstep.
`bigalloc` (not shown here) forces the heap to grow past its initial
mapping in one call - worth checking `frames`' free count is identical
before and after a `reset` that follows a grow, since `reset` is
specifically designed to reuse already-mapped pages rather than
re-`mapPage`-ing them (which would leak a frame per byte remapped).

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
- The heap's 16MB growth cap and 64KB-minimum-chunk sizing are both
  arbitrary constants (`mm/heap.mc`'s `gHeapCap` / `heapGrow`'s minimum
  chunk) picked for "plenty for a hobby kernel," not computed from actual
  available memory or tuned for any real workload.
- The heap only ever grows, never shrinks - `kfree` can leave large
  trailing free regions, but nothing unmaps pages and returns frames to
  the allocator once they're no longer needed.
- `mapPage` must not be called on a virtual address that falls inside
  boot.s's static 1GB identity map (PDPT index 0, i.e. any address below
  1GB) - the PD entries there are 2MB huge pages (PS bit set), and
  walking past one as if it pointed to a PT would read a garbage table
  address. Every caller today (the `map` shell command) stays above 1GB
  specifically to avoid this; nothing enforces it automatically yet.
- No unmap / page-table teardown - `mapPage` only ever adds entries and
  allocates frames for new table levels, never frees one back.
- The scheduler is cooperative only - tasks switch when they call
  `yield()`, not on a timer tick. A task that never yields (an infinite
  loop with no `yield()` in it, or a bug) starves every other task and
  the shell forever; nothing preempts it. Making the timer interrupt
  itself trigger a switch is the natural next step, but needs more than
  `switch_context` - the interrupt entry stub already saved the
  interrupted task's *full* trap frame (all GP registers plus what
  `iretq` needs) onto its stack before calling `interrupt_handler`, so a
  preemptive switch has to swap that whole frame, not just the
  callee-saved registers a voluntary `yield()` cares about.
- Tasks can't exit - `sched/task.mc`'s demo tasks are infinite loops on
  purpose; a `switch_context` `ret` into a task whose function actually
  returned would pop whatever's next on that stack as a return address,
  undefined behavior. No task-exit/cleanup path exists yet.
- The scheduler has a fixed 8-task table (`sched/task.mc`'s `gTasks`) and
  each task's 16KB stack is `kalloc`'d once and never freed - fine for a
  handful of long-lived demo tasks, not a real process model.
- `./build.sh` prints `warning: kmain.mc:...: unused function
  'interrupt_handler'` - a known false positive. `minicc`'s unused-
  function warning can't see that `interrupts.s` (a separate, hand-
  written assembly file) calls it; that's the same blind spot gcc's own
  `-Wunused-function` has for any non-`static` function.
- Keyboard support is lowercase letters, digits, space, and enter only (a
  small hand-built scancode table in `kmain.mc`), scancode set 1, no
  shift/modifier handling, no scrolling once the VGA cursor runs
  off-screen.
- x86-64/multiboot1 only, no Multiboot2 - still no real-hardware boot
  testing, only VirtualBox and QEMU (both via the `./build.sh iso` GRUB
  path) so far.
- The identity-mapped 1GB region is built with 2MB pages sized generously
  around what's actually needed (kernel at 1MB, VGA buffer at ~736KB) -
  not yet a real page-table layout a kernel would keep long-term.
- Interrupt handlers don't save/restore SSE/XMM state - fine today since
  nothing running at interrupt time uses floats, a real gap once
  something does.
