# MiniC kernel

The start of the OS kernel phase: a multiboot1-compliant kernel image
that boots to 64-bit long mode, handles real interrupts (timer +
keyboard), has a real heap (`kalloc`/`kfree`, splitting and two-way
coalescing, growing on demand by mapping fresh physical frames rather
than a fixed arena), a physical frame allocator driven by the multiboot
memory map, real dynamic paging (walking/creating PML4/PDPT/PD/PT chains
to map memory anywhere, not just inside the boot-time static identity
map), a preemptive scheduler switching between real kernel tasks on
their own stacks - the timer interrupt forces a switch, not just
voluntary `yield()` - with real blocking (`sleep()` takes a task out of
the round-robin until a given tick, not just "always ready"), a real
ring0/ring3 privilege boundary with a working syscall gate (`int 0x80`),
and runs a minimal interactive shell over VGA - all real, all verified
running in QEMU (byte-for-byte checked via the QEMU monitor's memory dump
and `sendkey`, not just "it didn't crash").

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
sched/            preemptive task scheduler
  switch.s           hand-written context switch (below what asm(...) can express)
  task.mc            Task table, createTask/yield/sleep, four demo tasks
syscall/          ring0/ring3 boundary
  usermode.s         hand-written ring3 entry (below what asm(...) can express)
  syscall.mc         syscall dispatcher + the one-shot ring3 test
shell/            the interactive shell
  shell.mc           cmd* functions + runCommand dispatch
```

- **`boot/boot.s`** - the multiboot header and the 32-to-64-bit
  transition, plus stashing EBX (multiboot's pointer to its info
  structure, handed to the kernel at entry and never overwritten since)
  into a MiniC global before calling `_start` - `_start` takes no
  parameters, same global-relay trick as `outb`'s port/value. Written
  directly against the real hardware, not through MiniC. Also where the
  GDT and TSS live: a ring3 code/data segment pair alongside the
  original ring0 ones, and a TSS whose `RSP0` field tells the CPU which
  stack to switch to on any ring3->ring0 transition (interrupt/exception
  firing while in ring3) - its own dedicated stack, deliberately not the
  one `_start`'s own C-style call chain runs on (see `int_stack_top`'s
  comment for the real bug that discovery fixed).
- **`boot/interrupts.s`** - entry stubs for the exceptions/IRQs the
  kernel handles (divide-by-zero, GPF, page fault, timer, keyboard): save
  every register, call into MiniC with the vector number + error code,
  restore, `iretq`. Same "below what `asm(...)` can express" reasoning as
  boot.s.
- **`sched/switch.s`** - `switch_context(oldRspOut, newRsp)`: pushes the
  current task's callee-saved registers, stashes the resulting stack
  pointer, switches to the next task's stack, pops its callee-saved
  registers, `sti`s, `ret`s. Same "below `asm(...)`" reasoning again - a
  context switch's entire point is preserving register state across what
  looks like one ordinary call into a different task's stack. The `sti`
  is what makes preemption actually work rather than deadlock - see the
  file's own comment for why it has to live exactly there (the one place
  execution transfers to *any* task, resuming or brand new) and not in
  MiniC's `yield()`.
- **`syscall/usermode.s`** - `run_ring3_test(entry, userStack)`: builds a
  fake `iretq` frame (the same trick `interrupts.s` already relies on for
  returning *from* an interrupt, just used here to enter ring3 for the
  first time) and jumps into it. Never returns the normal way - there's
  no ring3 "exit" mechanism yet (that needs a real process to exit *to*,
  milestone 12+) - instead `syscall.mc`'s syscall dispatcher, for one
  specific syscall number, pops this function's saved registers directly
  and `ret`s, faking a normal return without ever going back through
  `isr_syscall`'s `iretq` - the exact same "save an rsp, later load it
  and pop+ret" trick `switch_context` already uses, just for a one-shot
  privilege transition instead of an ongoing task switch.
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
  `ret`. `yield()` round-robins to the next task - called voluntarily by
  cooperative tasks, and *also* called from inside `isr/isr.mc`'s timer
  handler now, which is what makes this preemptive: since
  `interrupt_handler` is just an ordinary nested MiniC call on whichever
  task's stack the CPU happened to interrupt, calling `yield()` from
  inside it suspends that call exactly like a voluntary `yield()` would -
  the timer's full trap frame (pushed by `interrupts.s` below it on the
  stack) rides along for free and gets `iretq`'d correctly whenever the
  ring cascades back around to it. `sleep(ticks)` adds real blocking on
  top: a task marks itself `blocked` with a wake tick and yields away;
  `yield()`'s task-selection scan skips blocked tasks (waking one up the
  moment its wake tick arrives) instead of always picking "whoever's
  next" - task 0 (the shell) never blocks itself, which is what
  guarantees the scan always finds something runnable. `syscall/
  syscall.mc`'s `syscall_dispatch(num, arg1, arg2, arg3)` is what
  `boot/interrupts.s`'s `isr_syscall` calls for every `int 0x80` - the
  calling convention (`rax` = number in / return value out, `rdi`/`rsi`/
  `rdx` = up to three arguments) is this kernel's own, since going
  through a software interrupt gate rather than the `SYSCALL`/`SYSRET`
  instruction pair means there's no fixed convention to inherit. The
  `ring3` shell command runs a one-shot test through the whole path:
  `allocFrame` + `mapPage` (with the user bit, `0x06`) for a ring3-only
  stack, `run_ring3_test` into it, two `int 0x80` calls that print (and
  echo back the `cs` register's value, so the serial log has direct,
  checkable proof of which ring it actually ran in - `0x1B & 3 == 3`),
  then the exit syscall back to ring0. `shell/shell.mc`
  is the interactive
  shell (`help`/`clear`/`ticks`/`alloc`/`bigalloc`/`free`/`free <addr>`/
  `mem`/`reset`/`frame`/`unframe`/`frames`/`map`/`tasks`/`ring3`/
  `echo <text>`) built on `drivers/keyboard.mc`'s line buffer.
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
allocated 64 bytes at 0x5000c040
> alloc
allocated 64 bytes at 0x5000c090
> free
freed 0x5000c090
> mem
free: 0xff50 / 0x20000
> frames
free frames: 0x7bbe / 0x40000
> map
mapped 0x40000000 -> 0x422000, wrote/read 0xcafebabe
> tasks
task1: 0x1dc3 task2: 0x1dc4 task3: 0x2a38d9720 task4: 0x131 ticks: 0x3b8c
> ring3
entering ring3...
ring3 alive, cs=0x1b
ring3 alive again, cs=0x1b
back in ring0 - ring3 roundtrip verified
> echo hello world
hello world
```

Those first `alloc` addresses aren't right at the heap's base address
anymore, the way they were before milestone 8 - `sched/task.mc`'s four
demo tasks each `kalloc` a 16KB stack during boot, before the shell even
starts. That's 64KB of stacks alone, which is why `mem`'s total is
already `0x20000` (128KB, not the 64KB a fresh heap bootstraps with) by
the time anyone types a command - the stacks *just* pushed the heap into
its first on-demand growth before the shell even printed a prompt.

That `map` result proves the whole dynamic-paging chain: a frame beyond
the multiboot memory map's low end, mapped at a virtual address a full
1GB past anything boot.s set up statically, written through and read
back correctly - real PML4/PDPT/PD/PT walking and on-demand table
creation, not just "didn't crash."

That `tasks` result proves both preemption and blocking at once. task1
and task2 (cooperative, one increment then a voluntary `yield()`) stay
in exact lockstep with each other. task3 - a tight loop that never calls
`yield()` at all - is *six orders of magnitude* ahead, because it spins
for its *entire* timer slice every turn instead of giving up the CPU
after one increment; only possible if the timer genuinely forces control
away from it periodically (proving preemption) while it genuinely never
cooperates in between (proving that lead isn't secretly voluntary).
task4 (`sleep(50)` between increments) tracks `ticks / 50` almost
exactly - `0x3b8c` / 50 ≈ `0x131` - proving `sleep()` really removes it
from the round-robin for that many ticks rather than either blocking it
forever or being a no-op. `bigalloc` (not shown here) forces the heap to
grow further in one call - worth checking `frames`' free count is
identical before and after a `reset` that follows a grow, since `reset`
is specifically designed to reuse already-mapped pages rather than
re-`mapPage`-ing them (which would leak a frame per byte remapped).

That `ring3` result is the milestone 11 proof: `cs=0x1b` twice over
(`0x1B & 3 == 3` for the ring, `0x1B >> 3 == 3` for the GDT index -
exactly the ring3 code segment `boot.s` set up, not a coincidence or a
stale value) shows code genuinely ran at CPL 3, not just that a syscall
"happened" - a plain `int 0x80` would succeed from *any* ring targeting
a DPL=3 gate, so the CS readback is what actually rules out "still
secretly in ring0." Two round trips back to back rules out "worked once
by luck." And getting back to `>` afterward (not a hang, not another
crash) proves the exit path - the one-shot `switch_context`-style
save/restore trick in `syscall.mc` - works too, not just the initial
entry.

## Roadmap past milestone 10

Everything so far still runs in ring 0 sharing one page-table hierarchy -
"tasks" are kernel threads, not isolated processes. The longer-term
architecture (UNIX namespace/shell + NT-style kernel objects/handles +
microkernel-ish IPC/service isolation + a capability security model, all
exposed through a native MiniC API with a POSIX compatibility layer on
top) is planned in phases, each becoming a real numbered milestone when
its turn comes - not designed in detail this far ahead, the same way
milestones 1-10 were each scoped just before starting them:

1. ~~**Ring 3 + syscalls** (milestone 11) - GDT ring3 segments + a TSS
   (`RSP0` pointing at its own dedicated stack - see `int_stack_top`'s
   comment for why sharing one with the boot stack corrupted things) + an
   `int 0x80` syscall gate (DPL=3) + a minimal dispatcher. Address-space
   isolation deliberately *not* tackled yet (next item) - the entire
   static identity map is temporarily marked user-accessible instead, so
   an ordinary MiniC function and a freshly `mapPage`'d stack are enough
   to prove the privilege-transition mechanism itself works. Verified in
   QEMU: two `int 0x80` round trips read back `cs=0x1B` from ring3 (CPL 3,
   the real ring3 code segment - not just "a syscall happened", which
   would succeed from any ring), and a `switch_context`-style one-shot
   exit trick gets cleanly back to the shell afterward, not a hang.~~
2. **Per-process address spaces** (next up) - `mm/paging.mc` gains the
   ability to build a *new* PML4 hierarchy (not just add entries to the
   shared one), with the scheduler switching CR3 per task.
3. **A real `Process` concept + a loader** for a statically embedded
   program (no filesystem yet to load from disk).
4. **Kernel object model + per-process handle tables** (the NT-style
   piece) - userspace gets handles, never raw kernel pointers.
5. **IPC channels** between isolated processes - reuses milestone 10's
   `sleep()`/blocking mechanism for blocking `receive()`.
6. **Storage + VFS + a first filesystem** - ATA PIO driver, a minimal
   custom filesystem, then a VFS layer above it (FAT32/ext2 as later
   backends).
7. **Real `Process.spawn()` from disk**, once 3 and 6 both exist.
8. **Method-call syntax in MiniC itself** (a compiler milestone in the
   `minic` repo, not this one) - before building a native `File`/
   `Process`/`Socket`-style system API with real methods, then a thin
   POSIX compatibility shim over it.
9. **Capability/permission system** on top of the handle table, then
   security hardening (NX/ASLR/sandboxing).
10. **A real driver framework (PCI enumeration) + networking** (NIC
    driver, a from-scratch TCP/IP stack) - deliberately last: the
    largest remaining subsystem, with the fewest things depending on it.
11. **Service architecture + a real `init`** - the current hardcoded
    `shell/shell.mc` loop migrates to an actual userspace program once
    processes/IPC/VFS exist to support that; async I/O as a cross-cutting
    pass once sync I/O works everywhere.

Self-hosting (`minicc` compiled for MiniC OS's own target, running *on*
MiniC OS to compile something) isn't one of these phases - it's an
ongoing checkpoint to try after each major phase, with the real
prerequisites (native API, real file I/O, real process spawning) landing
around phase 7-8.

## Known limitations (on purpose, for now)

- The entire static identity map is user-accessible (`boot.s`'s PML4/
  PDPT/PD entries all carry the user bit) - meaning ring3 code today can
  read/write/execute the *whole kernel*, not just its own memory. This is
  milestone 11's explicitly temporary scope: proving the ring0/ring3
  mechanism works at all, before real per-process address-space isolation
  (milestone 12) makes "which pages are user-accessible" actually mean
  something.
- The ring3 test isn't scheduler-aware - it's not a `sched/task.mc` task,
  so `runRing3Test()` disables interrupts (`cli`) around the whole thing
  rather than risk a timer tick calling `yield()` mid-test and corrupting
  whichever kernel task's slot happened to be "current" when it started.
  Real ring3 tasks that coexist with preemption need a proper per-task
  register/CR3 frame (milestone 12+), not this one-shot workaround.
- Only one `int 0x80` syscall gate, two syscall numbers (1 = print, 2 =
  exit the one-shot test) - no real syscall table, no arguments beyond
  three plain integers, no pointer validation (a ring3 caller can pass
  any address as `arg1` for syscall 1 and it gets dereferenced directly).
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
- Preemption is timer-driven only, at a fixed ~10ms slice (every tick,
  no configurable quantum) - there's no priority, no fairness accounting
  beyond plain round-robin, and no way to pin a task or exempt it from
  being preempted (e.g. for a real critical section - none exist yet,
  but nothing would stop one from being preempted mid-update today).
- No nested-interrupt stack depth guard - a task getting preempted while
  already handling a previous nested interrupt is valid (each interrupt
  just pushes another frame on whatever stack is currently active) but
  unbounded in principle; 16KB per task has never come close to being an
  issue in practice, but nothing checks.
- Tasks can't exit - `sched/task.mc`'s demo tasks are infinite loops on
  purpose; a `switch_context` `ret` into a task whose function actually
  returned would pop whatever's next on that stack as a return address,
  undefined behavior. No task-exit/cleanup path exists yet.
- `sleep()` blocks by tick count only - no wait-on-event (I/O completion,
  another task finishing, a semaphore/mutex) yet, and no way for one task
  to wake another early. `yield()`'s scan for a runnable task is O(n) in
  the task count every call, fine for the handful of tasks here.
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
