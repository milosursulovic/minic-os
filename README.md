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
real per-process address-space isolation (each task can get its own PML4,
switched on every context switch, with a private region no other task can
see even at the identical virtual address), a real loader that copies a
hand-assembled machine-code blob into a freshly cloned address space and
schedules it as a genuine preemptible ring3 task (not a MiniC function
pretending), a kernel object model with per-process handle tables (ring3
code only ever sees a small integer, never a raw kernel pointer, and an
out-of-range or never-allocated handle is rejected rather than trusted),
and runs a minimal
interactive shell over VGA - all real, all verified
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
  paging.mc          dynamic PML4/PDPT/PD/PT paging, per-process address spaces
lib/              no-libc helpers
  strings.mc         streq/startsWith/parseHex/printHex
isr/              interrupt dispatch
  isr.mc             interrupt_handler, called from interrupts.s's stubs
sched/            preemptive task scheduler
  switch.s           hand-written context switch (below what asm(...) can express)
  task.mc            Task table (each with its own CR3), createTask/yield/sleep,
                     six demo tasks (two of them isolated processes)
syscall/          ring0/ring3 boundary
  usermode.s         hand-written ring3 entry (below what asm(...) can express)
  syscall.mc         syscall dispatcher
proc/             process loading + the kernel object model
  testprog.s         hand-assembled, position-independent ring3 "program" -
                     an opaque blob, not a MiniC function
  process.mc         Process table + spawnProcess(): the real loader
  object.mc          KernelObject table + per-process handle tables
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
  first time) and jumps into it. Never returns - there's no ring3 "exit"
  (no process-teardown mechanism exists yet at all). Milestone 11 also
  had a one-shot "exit" trick here (pop-and-`ret` back to a saved kernel
  rsp, bypassing `isr_syscall`'s `iretq`) for its now-retired one-shot
  ring3 demo - removed in milestone 13, see `proc/process.mc`'s comment
  for why keeping it around actively conflicted with real per-task ring3.
- **`proc/testprog.s`** - a tiny, hand-assembled ring3 "program": two
  `int 0x80` print syscalls with a message living inside the blob itself,
  then spins forever. Deliberately *not* MiniC compiled into the kernel
  image (that would just be milestone 11/12's demo trick again - kernel
  code with a borrowed address space, not a real loader) - the kernel
  treats it as opaque bytes it never inspects beyond copying them.
  Position-independent by construction (`lea reg, [rip+label]`, never an
  absolute `offset label`) since it gets *copied* to a virtual address
  that has nothing to do with wherever it happens to link inside this
  kernel image - RIP-relative offsets survive that copy unchanged as
  long as the whole blob moves as one contiguous unit.
- **`proc/process.mc`** - `spawnProcess(imageStart, imageEnd, loadVaddr,
  stackVaddr)`: the real loader. Clones a fresh address space
  (`mm/paging.mc`'s `cloneAddressSpace()`), maps and copies the image
  into it page by page (writing through each frame's own physical
  address - no need to switch into the new space first, the same
  reasoning `mapPageIn()`'s own comment gives), maps a one-page user
  stack, then creates a real scheduler task via `createTaskWithCr3()`
  whose kernel-mode entry (`processEntryTrampoline()`) does exactly one
  thing: call `run_ring3_test()` with the loaded entry point. From then
  on the task only ever runs in ring3, preemptible by the timer like any
  other task - the first time this kernel has proven that, rather than
  disabling interrupts around a one-shot ring3 excursion.
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
  instruction pair means there's no fixed convention to inherit.
  **Milestone 12** added
  `mm/paging.mc`'s `cloneAddressSpace()`: a fresh PML4 whose entry 0
  points at a fresh PDPT that shares PDPT[0] (the static identity map)
  and PDPT[1] (heap/dynamic-demo region) with the kernel's own PDPT -
  literally the same physical sub-tables, so kernel code/stack/heap stay
  reachable unchanged - while everything from PDPT[2] up (virtual
  addresses `>= 0x80000000`) starts out not-present, private to whichever
  task gets that PML4. `mapPage` got split into `mapPageIn(pml4Phys, ...)`
  (walks an *explicit* address space - safe from any task's context,
  since every page-table frame `allocFrame()` hands out lives inside the
  shared low-1GB identity map regardless of which CR3 is active) plus a
  `mapPage(...)` convenience wrapper for the kernel's own space, and a new
  read-only `translateIn(pml4Phys, vaddr)` walk for verifying which
  physical frame a given address space's mapping actually points at.
  `sched/task.mc`'s `Task` struct gained a `cr3` field; `createTask()`
  keeps giving new tasks the kernel's own space unchanged, while
  `createIsolatedTask()` calls `cloneAddressSpace()` first. `yield()`
  loads the incoming task's `cr3` (via a `[rip+global]`-relayed `mov cr3`)
  right before `switch_context()` - safe even while still running on the
  outgoing task's stack for that one instruction, since PDPT[0]/[1] are
  identical across every address space. Two demo tasks, `procAEntry`/
  `procBEntry`, both map the *same* virtual address (`0x80000000`) in
  their own private space to their own physical frame and write a
  different constant there - proving real isolation, not just separate
  stacks. **Milestone 13** added `proc/process.mc`'s `spawnProcess()`,
  wiring milestones 11 and 12 together into a real loader: `proc/
  testprog.s` is a hand-assembled, position-independent ring3 program -
  not a MiniC function, an opaque byte blob - that `spawnProcess()`
  clones a private address space for, maps and copies page by page, and
  schedules as a genuine task via `createTaskWithCr3()`, whose one-line
  kernel-mode entry (`processEntryTrampoline()`) calls `run_ring3_test()`
  and never returns to kernel mode again. This is the first ring3 code in
  this kernel that runs preemptible, coexisting with ordinary tasks under
  the same round-robin scheduler, rather than milestone 11's one-shot
  demo (`cli`'d around the whole thing, now retired - see below). Doing
  this surfaced a real bug: the retired demo and this new mechanism both
  used the *same* `TSS.RSP0` stack for ring3->ring0 transitions, safe
  only as long as at most one such transition could ever be "in flight"
  (suspended, not yet resumed) at a time - true for the demo alone, false
  the moment a real preemptible ring3 task could also be mid-suspension
  when the demo fired. Fixed by retiring the demo entirely rather than
  coordinating two incompatible mechanisms - see `syscall/syscall.mc`'s
  comment for the full diagnosis. **Milestone 14** added `proc/
  object.mc`: a kernel-wide `KernelObject` table plus a *separate*
  per-process handle table (`gHandleTables`, flattened to one array since
  MiniC has no 2D array declarations - process P's handle H lives at
  `gHandleTables[P * HANDLES_PER_PROCESS + H]`), the NT-style piece of
  the long-term plan. `resolveHandle(processIndex, handle)` is the one
  place a small ring3-supplied integer gets turned into an object index
  - bounds-checked and existence-checked, never trusted as a raw array
  index. `spawnProcess()` gives every new process a handle to itself for
  free, guaranteed to land in slot 0 (a fresh handle table is empty, so
  the first allocation into it always takes slot 0) - "handle 0 =
  myself," no syscall needed just to discover it. `sched/task.mc`'s
  `Task` gained a `processIndex` field (-1 for plain kernel tasks) so
  `syscall_dispatch` can find *whose* handle table a syscall's handle
  argument should be resolved against. New syscall number 3 (query
  handle) returns a process object's `taskIndex` - arbitrary but real
  ground truth, independently checkable against the `ps` shell command's
  own output - or the same `-1` sentinel `resolveHandle` returns
  whenever the handle doesn't check out. `shell/shell.mc`
  is the interactive
  shell (`help`/`clear`/`ticks`/`alloc`/`bigalloc`/`free`/`free <addr>`/
  `mem`/`reset`/`frame`/`unframe`/`frames`/`map`/`tasks`/`procs`/`ps`/
  `objs`/`echo <text>`) built on `drivers/keyboard.mc`'s line buffer.
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
Hello from a MiniC kernel!
hello from a LOADED process! 0xc0ffee
interrupts live
hello from a LOADED process! 0xc0ffee
handle 0 (self) -> taskIndex 0x7
handle 99 (invalid) -> 0xffffffffffffffff
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
> echo hello world
hello world
> procs
procA: 0xaaaaaaaa @phys 0x426000 procB: 0xbbbbbbbb @phys 0x429000
> ps
processes: 0x1 proc0 task=0x7 cr3=0x426000
> objs
objects: 0x1 obj0 type=0x1 dataIndex=0x0
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

The two `hello from a LOADED process!` lines, printed automatically
before the shell even shows its first prompt, are the milestone 13
proof: `proc/testprog.s` is hand-assembled machine code the kernel never
compiled, copied by `spawnProcess()` into a freshly cloned private
address space and jumped into via `run_ring3_test()` - two real `int
0x80` syscalls from inside that loaded, executing code, printing a
message that lives *inside the blob itself*, not a kernel string. Milestone
11's old `ring3` shell command (`cs=0x1b` readback proving CPL 3) is
retired - `spawnProcess()`'s task takes over that proof and more: it's
scheduled like any other task, interleaved with the boot sequence and
the rest of the demo tasks without any `cli` wrapping, and `ps`'s
`proc0 task=0x7 cr3=0x426000` confirms it's a real, queryable process
object, not just something that ran once and vanished.

The `handle 0 (self) -> taskIndex 0x7` / `handle 99 (invalid) ->
0xffffffffffffffff` lines are the milestone 14 proof, and they're a
matched positive/negative pair on purpose. The first shows the loaded
ring3 process resolving its *own* well-known handle (0) all the way
through to a piece of kernel ground truth (`taskIndex`) - `0x7` is
exactly what `ps` independently reports for this same process later in
the same session, which is the actual point: two different paths (a
ring3 syscall's handle lookup, and the shell's direct table read) landed
on the identical number, meaning the handle genuinely resolved through
`gObjects`/`gProcesses` rather than being hardcoded or coincidental. The
second shows a handle number that was never allocated (99, past the
8-per-process limit *and* never assigned even if it weren't) coming back
as the same `-1` sentinel `resolveHandle()` returns for any invalid
handle - not garbage, not a crash - proving the bounds/existence check
is real, not decorative. `objs`' `obj0 type=0x1 dataIndex=0x0` confirms
the object side directly: exactly one `KernelObject` exists, its type is
`OBJ_PROCESS` (`1`), and it points at `gProcesses[0]` - the same process
`ps` and the ring3 self-handle both already agreed on.

That `procs` result is the milestone 12 proof: `procA` and `procB` both
map the identical virtual address (`0x80000000`) in their own private
address space, each to a physical frame it allocated itself, and each
keeps reading back the exact constant it wrote there (`0xaaaaaaaa` /
`0xbbbbbbbb`) forever, interleaved by the same preemptive scheduler
proven in milestone 9 - if CR3 switching weren't actually happening, or
the two "processes" secretly shared one address space, they'd be
fighting over one real page-table entry and whichever task wrote most
recently would win for *both* reads. The `@phys` addresses being
different (`0x426000` vs `0x429000`) is the stronger half of the proof -
not just "two different numbers came back," but that the *same virtual
address* genuinely resolves to two different physical frames depending
on which address space is asking, exactly what "isolation" has to mean.
Repeating `procs` later in the same session (after `tasks`, `alloc`, and
`map` all ran in between) showed byte-for-byte identical values and
physical addresses - stable across scheduler churn, not a one-shot
fluke. Same for `ps`'s output, checked both right after boot and again
several seconds (and many preemption cycles of the loaded process) later
- identical every time, including through the exact regression sequence
that used to trigger the milestone-13 RSP0 bug before it was fixed.

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
2. ~~**Per-process address spaces** (milestone 12) - `mm/paging.mc` gained
   `cloneAddressSpace()`, building a *new* PML4 that shares the kernel's
   own PDPT[0]/PDPT[1] (identity map + heap) but leaves everything from
   PDPT[2] up (`vaddr >= 0x80000000`) private, populated on demand via a
   new `mapPageIn(pml4Phys, ...)` that walks an explicit address space
   instead of always the global one. `sched/task.mc`'s `Task` gained a
   `cr3` field; `yield()` loads it before every `switch_context()`.
   Verified in QEMU: two demo tasks (`procAEntry`/`procBEntry`) map the
   *identical* virtual address in their own private space to their own
   physical frame and each reads back its own distinct constant forever -
   critically, `translateIn()` shows that same virtual address resolving
   to two genuinely different physical addresses depending on which
   address space asks, not just "two different numbers came back."~~
3. ~~**A real `Process` concept + a loader** (milestone 13) -
   `proc/process.mc`'s `spawnProcess()` treats a byte range as an opaque
   blob (`proc/testprog.s`, hand-assembled, not MiniC), clones a private
   address space for it, maps and copies it in page by page, and
   schedules a real task whose only kernel-mode act is entering ring3 at
   the loaded address - the first ring3 code in this kernel that's
   genuinely preemptible rather than run under a one-shot `cli`. Doing
   this retired milestone 11's `ring3` shell command entirely: it and the
   new mechanism both needed the same `TSS.RSP0` stack for ring3->ring0
   transitions, safe only with at most one such transition ever in flight
   - true when the demo was the only ring3 path, false once a real
   preemptible ring3 task could also be mid-suspension when the demo
   fired. A real bug found and fixed this way, not a hypothetical.
   Verified in QEMU: the loaded blob's own embedded message printed twice
   from ring3 at boot, and the shell stayed fully responsive through a
   full regression pass afterward (the exact sequence that hung before
   the fix).~~
4. ~~**Kernel object model + per-process handle tables** (milestone 14,
   the NT-style piece) - `proc/object.mc`'s `KernelObject` table plus a
   *separate* per-process handle table (`resolveHandle(processIndex,
   handle)` is the one place a ring3-supplied integer becomes an object
   index - bounds-checked, never trusted directly). Every process gets a
   handle to itself for free in the well-known slot 0. New syscall number
   3 resolves a handle within the *calling* process's own table.
   Verified in QEMU: a loaded process resolving its own handle 0 gets
   back its `taskIndex` (`0x7`), independently matching what the `ps`
   shell command reports for the same process moments later - two
   different paths landing on the identical number, not a coincidence -
   while an invalid handle (99, never allocated) comes back as the same
   `-1` sentinel every failure path uses, not garbage or a crash.~~
5. **IPC channels** (next up) between isolated processes - reuses milestone 10's
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

- The shared region every address space carries (PDPT[0]/PDPT[1] - the
  static identity map plus the heap) is still marked user-accessible
  (`boot.s`'s PML4/PDPT/PD entries all carry the user bit), so ring3 code
  can still read/write/execute the *whole kernel and every other task's
  shared memory* - milestone 12 gave each task a genuinely private
  region (`vaddr >= 0x80000000`), but didn't restrict what the *shared*
  region looks like to unprivileged code. That needs a real memory-
  protection pass once security/capability work is underway (roadmap
  phase IX), not just an address-space topology change.
- **Only one ring3 process is safe to run at a time.** `boot.s`'s TSS has
  a single `RSP0` - correct for "at most one suspended ring3->ring0
  transition can be in flight at once" (true with exactly one ring3 task,
  which is what removed milestone 11's demo's conflict with milestone
  13's real one), but a *second* concurrently-scheduled ring3 process
  would reintroduce exactly that class of bug between itself and the
  first. Needs a per-task `RSP0`, switched alongside `cr3` in `yield()`,
  before `proc/process.mc`'s `spawnProcess()` can safely be called more
  than once. Not yet needed - only one process is spawned today - but a
  real constraint the next milestone that spawns multiple processes must
  address, not an oversight to rediscover the hard way.
- `cloneAddressSpace()`'s PML4/PDPT/PT frames (and a spawned process's
  image/stack frames, and now its `KernelObject`/handle-table entries)
  are never freed - there's no process teardown at all yet (nothing
  calls `freeFrame` on any of it, no "close handle"/"free object"
  exists), consistent with the kernel having no process *exit*
  mechanism at all so far.
- `proc/object.mc`'s tables are fixed-size arbitrary constants (8
  `KernelObject`s total, 8 handles per process, matching `gProcesses`'
  4-process cap) - fine for today's one loaded process, would need real
  sizing (or dynamic growth) once more than a handful of objects/
  processes exist at once.
- Only one kernel object type exists (`OBJ_PROCESS`) - a `Task` (the
  scheduler's own thread-of-control concept) isn't a kernel object in
  its own right yet, unlike real NT where Process and Thread are
  separate object types. Not a gap so much as not-yet-needed: nothing
  today creates more than one thread per process to distinguish.
- Only one `int 0x80` syscall gate, two syscall numbers (1 = print, 3 =
  resolve a handle) - no real syscall table, no arguments beyond three
  plain integers, no pointer validation (a ring3 caller can pass any
  address as syscall 1's `arg1` and it gets dereferenced directly).
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
