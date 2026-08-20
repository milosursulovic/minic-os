# MiniC-OS kernel

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
real, compiled program into a freshly cloned address space
and schedules it as a genuine preemptible ring3 task (not a function of
the *kernel's own* address space pretending), a kernel
object model with per-process handle tables (ring3
code only ever sees a small integer, never a raw kernel pointer, and an
out-of-range or never-allocated handle is rejected rather than trusted),
IPC channels between isolated processes with a real blocking receive
(reusing the exact same scheduler mechanism `sleep()` already proved,
just generalized to a second kind of wake condition), a legacy ATA PIO
disk driver doing genuine sector-granular reads and writes against a
real (emulated) block device, a minimal custom filesystem ("MiniFS")
on top of it with real multi-file create/read/list persisted to disk, a
VFS layer above that routing path-based requests to either MiniFS (real
disk I/O) or a live-kernel-state device backend through the identical
function call, real `process.spawn()` from disk (a second, independently
loaded ring3 process, its image bytes read through the VFS from a real
MiniFS file rather than a pointer into the kernel's own image, running
concurrently with the first thanks to a per-task `TSS.RSP0` fix), a real
native "File" API - `file_write(&msg_file, ...)`/`file_read(&msg_file, ...)`
(a plain function taking an explicit `self` pointer, C's own shape for
what MiniC once expressed as `msg_file.write(...)` method-call sugar)
from inside an actual ring3 process, wrapping two new syscalls rather
than raw syscall numbers - real `channel`/`process` functions too
(`channel_receive(&spawn_trigger)` blocking a ring3 process on a real
syscall for the first time, `process_spawn(&child_image, ...)` letting a
ring3 process launch another one itself, not just the kernel/shell doing
it on its behalf), and a thin POSIX-shaped shim (`open`/`read`/`write`/
`close`, real position-tracking across multiple calls) implemented
entirely in ring3 C on top of the native File API, no kernel changes
needed, a real capability/permission
system on top of the handle table (a handle carries fixed rights,
granted once at open time - a ring3 process handed a receive-only
channel handle genuinely cannot use it to send, not just "isn't
expected to"; the same real enforcement now covers `process` handles
too, including a ring3 process opening a genuinely rights-less handle
to ANOTHER process and having every query through it correctly
rejected), real memory-protection hardening on the shared kernel
region (a cloned, ring3-capable address space's copy of the kernel's
own identity-map/heap PDPT entries has the user bit stripped, so a
deliberate ring3 write into that region takes a real page fault instead
of silently succeeding), real NX (no-execute) enforcement on every
dynamically-mapped data region including each ring3 process's own user
stack (EFER.NXE set once at boot, the classic stack-hardening win - a
`ret` opcode written onto a process's own stack and jumped to takes a
real page fault with the CPU's own instruction-fetch bit set in its
error code, not a silent execution), real PCI bus enumeration (walking
the legacy CONFIG_ADDRESS/CONFIG_DATA config-space mechanism to discover
its own hardware for the first time, rather than trusting a hardcoded
I/O port - every device it finds checks out against QEMU's own default
machine model, including the emulated NIC that networking will need), a
real driver for that NIC (an Intel e1000, initialized over PCI and its
memory-mapped register file mapped into the kernel's own address space
for the first time - the real MAC address and link-up status read back
match QEMU's own known-real hardware state, not fabricated numbers), real
DMA-based packet TX/RX through that driver (a hand-crafted ARP request
transmitted, its completion confirmed by the hardware's own descriptor
status bit, and a genuine reply received back from QEMU's real networking
backend - the sender address in that reply is QEMU's own well-documented
default gateway, not anything this kernel invented), a real ARP resolver
on top of it (a genuine cache - a second resolve of the same address
returns instantly with zero packets sent, checkable by real elapsed
ticks, not just "returned the same answer twice" - that generalizes to
any target address and correctly reports failure, not a hang, for one
nothing answers), a real IPv4 layer with a genuine ICMP echo (ping) round
trip to the gateway (real header construction, a real RFC 791 checksum,
this kernel's own first concept of "my IP address" - the reply's
identifier and sequence number are checked to genuinely match what was
sent, not just "something came back"), a real UDP layer (a real pseudo-
header checksum binding the datagram to its own source/destination
addresses) with a genuine DNS query to QEMU's own resolver proxy as
proof - a real answer, from a real upstream DNS resolution, not
self-validation - a real TCP client (the first genuinely stateful
transport protocol here, and the first to talk to something off the
local subnet at all - a real 3-way handshake, a real HTTP GET, and a
real HTTP reply from an actual internet host reached through the SLIRP
gateway, not a self-contained round trip) with a real DNS-resolved
target rather than a hardcoded address, and
runs a minimal
interactive shell over VGA - all real, all verified
running in QEMU (byte-for-byte checked via the QEMU monitor's memory dump
and `sendkey`, not just "it didn't crash").

**A note on language history.** Milestones 1 through 34 (everything up to
and including the networking phase) were built in MiniC, a small
hand-rolled language with its own compiler (`minicc`, developed
alongside this kernel in a sibling repo). After milestone 34 shipped,
the entire kernel - every `.mc` file, without exception - was rewritten
by hand into freestanding C, and the MiniC compiler and its own docs
site were retired. This wasn't a change of design philosophy: the
"no external libraries, everything hand-written" rule below is exactly
as true today as it always was, the architecture (paging, scheduler,
syscalls, object/handle model, drivers, filesystem, networking) is
functionally unchanged, and every milestone's own verification was
re-run and re-confirmed against the rewritten kernel before it replaced
the MiniC original. What changed is purely the implementation language,
for faster day-to-day development. The roadmap below keeps the real
MiniC-era milestone writeups exactly as they originally shipped (some
example code blocks below show MiniC's own `type.method(self, args)`
syntax or `import` statements - these are the real, historically
accurate MiniC source at the time each milestone landed, not stale
mistakes) - see "Roadmap past milestone 10" for where the rewrite itself
is marked. New code from that point on is C: `type_method(self, args)`
for what used to be a method call, `#include` instead of `import`, a
real per-file `.h`/`.c` split instead of one flat whole-program
namespace, and real GCC inline asm instead of MiniC's operand-less
`asm("...")` staged through global variables.

## Why there's hand-written assembly here

Multiboot drops you in 32-bit protected mode; this kernel runs in 64-bit
long mode. Getting from one to the other - loading a GDT, building page
tables, enabling long mode, far-jumping into a 64-bit code segment - is
program *structure*: which instruction stream the CPU is even executing,
not a value any function body (C's real inline asm included) computes
and returns from. Every kernel project hand-writes an equivalent of
this, even ones written in Rust or Zig. Same reasoning covers interrupt
entry: saving/restoring full register state and normalizing "sometimes
the CPU pushes an error code, sometimes it doesn't" into one common call
is calling-convention plumbing that has to exist *outside* any single
function's own prologue/epilogue, not something even a `__asm__`
statement inside a C function can arrange for its own caller. A context
switch is the same kind of gap: it means preserving one task's register
state *across* what looks to the C compiler like one ordinary call,
into a completely different task's stack - real inline asm can bind
operands and clobbers for a single instruction sequence, but "suspend
this call stack, resume a different one later" is a control-flow shape
no C function signature can express for itself.

## Project layout

One folder per subsystem, with room to split further as each one grows
(e.g. `shell/` gaining one file per command, `isr/` gaining one per
vector) rather than everything staying flat in a single file. Each
subsystem is now a real `.c`/`.h` pair rather than one flat MiniC
module: C has no whole-program `import` merge the way MiniC did, so a
file's public surface has to be declared explicitly in its own header
instead of just being visible kernel-wide by default - `disk/minifs.c`
exposing a clean `fs_superblock_info()`/`fs_list_entry()` API instead of
letting `shell.c` reach into its on-disk struct layout directly (see
below) is the real encapsulation win that split buys, one MiniC's flat
namespace never offered. Every header in this kernel wraps its
declarations in `#pragma GCC visibility push(hidden)`/`pop` - not a
stylistic choice, load-bearing: it matches the Makefile's own
`-fvisibility=hidden`, and without it a plain `extern` declaration of a
symbol some other translation unit defines would compile to a
GOT/PLT-indirected reference that `as --32` can't assemble at all (see
`CLAUDE.md` for the fuller mechanics). `types.h` (`u8`-`u64`/`i8`-`i64`
typedefs over `<stdint.h>`, plus `<stdbool.h>`/`<stddef.h>`) is the one
header nearly every other file includes first.

```
types.h           u8-u64/i8-i64 typedefs, plus bool/NULL - included almost everywhere
kmain.c           entry point (_start) + #includes wiring everything together
boot/             hand-written assembly - below what asm(...) can express
  boot.s            multiboot header, 32-to-64-bit transition. Milestone
                     28: also sets EFER.NXE (bit 11) alongside the
                     existing EFER.LME, in the same rdmsr/wrmsr round
                     trip - required before any page table entry
                     anywhere can safely set the NX bit
  interrupts.s       ISR/IRQ entry stubs (save/restore, call into C)
  linker.ld          places the multiboot header + code at the 1MB load address
drivers/          hardware setup and I/O
  io.c/.h            VGA text buffer, serial port, raw in/out port I/O.
                     Milestone 29: outl/inl (32-bit port I/O), PCI config
                     space is dword-addressed. Real GCC inline asm has
                     real operand binding, so outb/inb etc. bind the
                     port/value straight into the instruction now -
                     no more MiniC-era relay through a module global
  interrupts_init.c/.h  IDT + 8259 PIC remap + PIT reconfiguration
  keyboard.c/.h      scancode table + the shell's line buffer
  pci.c/.h           Milestone 29 (Phase X's first step): PCI bus
                     enumeration via the legacy CONFIG_ADDRESS/
                     CONFIG_DATA mechanism - bus 0 only, no bridge
                     recursion yet (see Known limitations). Milestone
                     30: pci_config_write_dword() (this kernel's first PCI
                     config space WRITE) and pci_read_bar0()
net/              networking (milestone 30 onward)
  e1000.c/.h         the e1000 NIC driver - PCI enable, MMIO register
                     mapping, real MAC/link-status readback. Milestone
                     31: real TX/RX descriptor rings (packed structs
                     matching the hardware's own 16-byte layout),
                     e1000_send()/e1000_receive() - a genuine packet round
                     trip, verified via a hand-crafted ARP request/reply
                     (see Known limitations for what's still ahead)
  arp.c/.h           milestone 32: a real ARP resolver - a genuine
                     cache, arp_resolve() working for any target address,
                     a real tick-bounded timeout on a miss. Client
                     (resolver) only, no responder (see Known limitations)
  ip.c/.h            milestone 33: real IPv4 header build + the RFC 791
                     checksum algorithm, and g_my_ip - this kernel's own
                     first concept of "my IP address" (still static, no
                     DHCP - see Known limitations)
  icmp.c/.h          milestone 33: icmp_ping() - a real echo request/reply
                     round trip, the minimal verification vehicle for
                     "does IP actually work," the same relationship
                     milestone 31's ARP had to proving TX/RX worked
  udp.c/.h           milestone 34: real UDP - udp_checksum() (the real
                     pseudo-header algorithm), udp_build_header(),
                     udp_send()/udp_receive()
  dns.c/.h           milestone 34: dns_query() - a hand-crafted,
                     minimal DNS query, the verification vehicle for
                     UDP the same way ICMP's ping was for IP. Not a
                     real DNS client (see Known limitations)
mm/               memory management
  heap.c/.h          kalloc/kfree free-list allocator, grows on demand via
                     mm/paging.c; every grown page is PAGE_NX (milestone 28)
  frames.c/.h        multiboot memory map parser + physical frame bitmap allocator
  paging.c/.h        dynamic PML4/PDPT/PD/PT paging, per-process address
                     spaces, per-task TSS.RSP0 (set_tss_rsp0). Milestone 26:
                     clone_address_space() strips the user bit from its
                     copy of the kernel's shared PDPT[0]/[1] entries -
                     boot.s's own static map and g_pml4_phys (never run in
                     ring3) are untouched, only the copy a cloned,
                     ring3-capable address space gets. Milestone 28:
                     PAGE_NX (PTE bit 63), OR'd across levels (unlike the
                     user bit's AND) so only the leaf entry ever needs it -
                     map_page_in()'s flags mask widened to pass it through,
                     translate_in()'s own physical-address extraction fixed
                     to mask it back out (a real bug this milestone found:
                     it only cleared the low 12 bits before, so a
                     PAGE_NX-marked page's reported physical address came
                     back with bit 63 still stuck in it)
lib/              no-libc helpers
  strings.c/.h       streq/starts_with/strlen_/parse_hex/print_hex/format_hex -
                     strlen_ rather than strlen, so it can never collide
                     with the compiler's own builtin knowledge of that
                     exact name even under -fno-builtin
isr/              interrupt dispatch
  isr.c/.h           interrupt_handler, called from interrupts.s's stubs.
                     Milestone 28: vector 14 (page fault) now also prints
                     the raw error code - bit 4 distinguishes an
                     instruction-fetch (NX) violation from a read/write one
sched/            preemptive task scheduler
  switch.s           hand-written context switch (below what asm(...) can express)
  task.c/.h          Task table (each with its own CR3 + TSS.RSP0), create_task/
                     yield/sleep_ticks/channel_receive, eight demo tasks (four processes)
syscall/          ring0/ring3 boundary
  usermode.s         hand-written ring3 entry (below what asm(...) can express)
  syscall.c/.h       syscall dispatcher: print, handle-query, vfs_read/
                     vfs_write (milestone 22, numbers 4/5), spawn/
                     channel_send/channel_receive (milestone 23, numbers
                     6/7/8 - channel_receive is this kernel's first
                     BLOCKING syscall, reusing channel_receive()'s
                     existing yield()/switch_context() mechanism
                     unchanged since syscall_dispatch runs as an
                     ordinary nested call within the calling ring3
                     task's own context), and open_channel (milestone 25,
                     number 9) - numbers 7/8 now take a real, rights-
                     checked HANDLE instead of a raw channel index.
                     Milestone 27: number 3 (query) now checks
                     RIGHT_QUERY too (previously granted but never
                     verified), and new number 10 (open_process) mints a
                     handle to ANOTHER process with caller-requested
                     rights intersected against what's actually
                     grantable
proc/             process loading + the kernel object model + IPC
  ring3prog.c        a real, gcc-compiled ring3 "program" - the loaded
                     blob; see ring3.ld/ring3blob.s for how a compiled
                     program's separate ELF sections get flattened into
                     the one contiguous blob the loader below expects.
                     As of milestone 22, also the first real use of the
                     native File API for something real: a `file` struct
                     with `file_write(&self, ...)`/`file_read(&self, ...)`
                     wrapping the new syscalls - plain C functions taking
                     an explicit `self` pointer (MiniC's own version of
                     this file used real `.write()`/`.read()` method-call
                     syntax on the same struct; C has no method-call
                     sugar, so `type_method(&self, args)` is this
                     codebase's standing convention for it now, e.g.
                     `channel_send(&spawn_trigger, value)`). Milestone 23
                     added `channel`/`process` the same way -
                     `channel_receive(&self)` blocks on a real syscall
                     until the shell's `ring3go` command triggers it,
                     then `process_spawn(&self, ...)` launches a second
                     copy of this same blob as an independent process.
                     Milestone 24 added a thin POSIX shim (open/read/
                     write/close, plain free functions matching POSIX's
                     own shape rather than extending the native one)
                     entirely in this file, layered on top of File - no
                     new syscalls, no kernel changes. Milestone 25 added
                     `channel_open(&self, ...)`, and deliberately
                     exercises an unauthorized `channel_send(&self, ...)`
                     on the resulting receive-only handle to prove real
                     rights enforcement, not just handle-vs-index
                     indirection. Real GCC inline asm has real operand
                     binding, so `do_syscall()` passes its arguments
                     straight into `rax`/`rdi`/`rsi`/`rdx` via register
                     variables - none of the MiniC-era global-staging
                     trick every `asm(...)` block here used to need. See
                     `ring3.ld` below for a genuine bug the C rewrite hit
                     compiling this file
  ring3.ld           standalone linker script for ring3prog.c's own
                     link (keeps .text/.rodata/.data/.bss contiguous;
                     build.sh's objcopy step forces .bss to be
                     represented as real zero bytes too, see milestone
                     24's bug notes below). Also pins `_start` to a
                     dedicated `.text.start` section at offset 0 - a real
                     bug found during the C rewrite: spawn_process()
                     always jumps to byte 0 of the loaded image, which
                     happened to always be the entry point under MiniC's
                     own single-pass codegen but isn't guaranteed under
                     gcc, which placed a different function first once
                     this file grew past one function. `.text.start`
                     (paired with `__attribute__((section(".text.start")))`
                     directly on `_start()` in ring3prog.c) pins it to
                     offset 0 regardless of source order or any future
                     gcc reordering
  ring3blob.s        wraps the objcopy'd flat blob in g_test_prog_start/
                     g_test_prog_end, same marker names every earlier
                     milestone's hand-assembled testprog.s exported
  process.c/.h       Process table + spawn_process()/spawn_process_from_path():
                     the real loader, from a pointer range or a VFS path
                     (g_loaded_image_buf is 16KB as of milestone 24, not
                     4096 - see milestone 24's bug notes below). Jumps to
                     the loaded image's byte 0 as its entry point - see
                     ring3.ld above for the real bug that assumption hit
                     once the loaded program was gcc-compiled instead of
                     MiniC-compiled
  object.c/.h        KernelObject table + per-process handle tables;
                     milestone 25 gave each `handle` a real `rights`
                     bitmask, fixed forever at grant time - the first
                     step of the roadmap's Phase IX (capability/
                     permission work). Milestone 27 removed
                     resolve_handle() (unused once every caller also
                     needed the handle's rights, not just its object
                     index - each syscall inlines its own check now).
                     `g_handle_tables` is a genuine 2D C array
                     (`handle g_handle_tables[4][8]`) - process P's
                     handle H lives at `g_handle_tables[P][H]` directly,
                     no manual flattening needed
  channel.c/.h       Channel table + channel_send/channel_has_message
disk/             storage
  ata.c/.h           legacy ATA PIO driver - real sector read/write
  minifs.c/.h        MiniFS: a minimal custom filesystem (superblock +
                     flat directory + contiguous per-file storage).
                     Exposes a small `fs_superblock_info()`/
                     `fs_list_entry()` API for `ls` rather than handing
                     callers the raw on-disk `superblock`/`dir_entry`
                     structs - a real encapsulation boundary C's
                     separate translation units give for free, unlike
                     the MiniC-era version's flat single-namespace
                     `import` model where `shell.mc` read `minifs.mc`'s
                     on-disk structs directly
  vfs.c/.h           VFS: path -> mount -> backend routing (tag+if/else)
  devfs.c/.h         the "/devices" backend - live kernel state, no disk
shell/            the interactive shell
  shell.c/.h         cmd* functions + run_command dispatch
```

- **`boot/boot.s`** - the multiboot header and the 32-to-64-bit
  transition, plus stashing EBX (multiboot's pointer to its info
  structure, handed to the kernel at entry and never overwritten since)
  into a real C global (`g_multiboot_info_ptr`) before calling `_start` -
  `_start` takes no parameters. Written directly against the real
  hardware, not through C. Also where the GDT and TSS live: a ring3
  code/data segment pair alongside the original ring0 ones, and a TSS
  whose `RSP0` field tells the CPU which stack to switch to on any
  ring3->ring0 transition (interrupt/exception firing while in ring3) -
  boot.s only ever sets it *once*, to its own dedicated stack
  (`int_stack_top`, deliberately not the one `_start`'s own C-style call
  chain runs on - see that label's comment for the real bug that
  discovery fixed), as a safe initial value before scheduling begins.
  From milestone 19 on, `mm/paging.c`'s `set_tss_rsp0()` repoints it
  per-task at every context switch instead - `tss_start` is exported
  (`.global`) specifically so that C function can reach it directly.
- **`boot/interrupts.s`** - entry stubs for the exceptions/IRQs the
  kernel handles (divide-by-zero, GPF, page fault, timer, keyboard): save
  every register, call into C with the vector number + error code,
  restore, `iretq`. Same "below what `asm(...)` can express" reasoning as
  boot.s.
- **`sched/switch.s`** - `switch_context(old_rsp_out, new_rsp)`: pushes the
  current task's callee-saved registers, stashes the resulting stack
  pointer, switches to the next task's stack, pops its callee-saved
  registers, `sti`s, `ret`s. Same "below `asm(...)`" reasoning again - a
  context switch's entire point is preserving register state across what
  looks like one ordinary call into a different task's stack. The `sti`
  is what makes preemption actually work rather than deadlock - see the
  file's own comment for why it has to live exactly there (the one place
  execution transfers to *any* task, resuming or brand new) and not in
  `task.c`'s `yield()`.
- **`syscall/usermode.s`** - `run_ring3_test(entry, user_stack)`: builds a
  fake `iretq` frame (the same trick `interrupts.s` already relies on for
  returning *from* an interrupt, just used here to enter ring3 for the
  first time) and jumps into it. Never returns - there's no ring3 "exit"
  (no process-teardown mechanism exists yet at all). Milestone 11 also
  had a one-shot "exit" trick here (pop-and-`ret` back to a saved kernel
  rsp, bypassing `isr_syscall`'s `iretq`) for its now-retired one-shot
  ring3 demo - removed in milestone 13, see `proc/process.c`'s comment
  for why keeping it around actively conflicted with real per-task ring3.
- **`proc/ring3prog.c`** - the loaded ring3 "program", real C compiled
  standalone via gcc (see the Makefile's dedicated `proc/ring3prog.bin`
  rule and `ring3.ld`). Milestone 21 first replaced a hand-assembled
  version (`proc/testprog.s`) with a compiled one - then MiniC via
  `minicc --freestanding`, gcc since the C rewrite. Its original
  behavior still opens every run: two `int 0x80` print syscalls with an
  embedded message, a self-handle query and an invalid-handle query each
  printed - milestones 22-25 (see the tree comment above) then layered
  the native File/Channel/Process API, the POSIX shim, and the
  rights-enforcement demos directly on top before it spins forever, all
  in this one file, no kernel changes. Still deliberately *not* linked
  straight into the kernel image the way `kmain.c` is (that would put its
  `.text` and `.rodata` in different places than a simple byte-range copy
  can handle - see `proc/ring3.ld`) - the kernel still treats the final
  flattened blob as opaque bytes it never inspects beyond copying them,
  same as before. Position-independent by construction: gcc's `-fPIC`
  (the Makefile's CFLAGS) addresses every global/string `[rip+label]` and
  every call as a plain relative `call name`, the same RIP-relative
  convention MiniC's own codegen always used unconditionally - but that
  guarantee only holds here because `ring3.ld`'s standalone link keeps
  `.text`/`.rodata`/`.data`/`.bss` contiguous; if this were linked into
  `kernel.elf` directly instead, `ld` would separate them from every
  other object's same-named sections and break the RIP-relative offsets
  between them.
- **`proc/ring3.ld`** / **`proc/ring3blob.s`** - the standalone link +
  `objcopy -O binary` + `.incbin` pipeline that turns `ring3prog.c`'s
  compiled output into one flat, contiguous blob and re-exports it under
  the same `g_test_prog_start`/`g_test_prog_end` symbol names the old
  hand-assembled `testprog.s` always used - see the Makefile for the
  exact steps. Nothing in `proc/process.c`, `kmain.c`, or `shell.c` needed
  to change: only how those bytes get produced is new. `ring3.ld`'s
  `.text.start` section (see the tree comment above for the real bug
  this fixed) is the one place this pipeline itself changed shape for
  the C rewrite.
- **`proc/process.c`** - `spawn_process(image_start, image_end, load_vaddr,
  stack_vaddr)`: the real loader. Clones a fresh address space
  (`mm/paging.c`'s `clone_address_space()`), maps and copies the image
  into it page by page (writing through each frame's own physical
  address - no need to switch into the new space first, the same
  reasoning `map_page_in()`'s own comment gives), maps a one-page user
  stack, then creates a real scheduler task via `create_task_with_cr3()`
  whose kernel-mode entry (`process_entry_trampoline()`) does exactly one
  thing: call `run_ring3_test()` with the loaded entry point (byte 0 of
  the image - see `ring3.ld`'s tree comment above for the real bug that
  assumption surfaced during the C rewrite, fixed there rather than
  here). From then on the task only ever runs in ring3, preemptible by
  the timer like any other task - the first time this kernel has proven
  that, rather than disabling interrupts around a one-shot ring3
  excursion.
- **`kmain.c`** and its includes - everything past "here's the vector
  number," in ordinary C. `kmain.c` itself is just the entry point
  (`_start`) plus a `#include` of every module's header above, using
  nothing beyond ordinary freestanding C (`volatile`,
  `__attribute__((packed))` structs, pointer indexing, real GCC inline
  asm - `__asm__ volatile(...)` with real operand binding, not MiniC's
  operand-less `asm("...")` staged through globals - for the handful of
  raw instructions - `out`/`in`/`lidt`/`sti`/`invlpg`/reading `cr2`/`cr3` -
  freestanding C still has no other syntax for). `mm/paging.c`'s `map_page`
  walks/creates a PML4->PDPT->PD->PT chain of 4KB pages, allocating fresh
  frames (from `mm/frames.c`) for any missing table level - reads CR3
  once at boot into a real C global the same way `g_multiboot_info_ptr`
  works, since every physical address it touches, table pages included,
  lands inside the boot-time flat 1GB identity map and so is directly
  dereferenceable as an ordinary pointer, no temporary-mapping trick
  needed. `mm/frames.c`'s multiboot parser uses `multiboot_info`/
  `mmap_entry`, both `__attribute__((packed))` - `mmap_entry` in
  particular has a genuinely unaligned field by the real spec, exactly
  the case `packed` exists for. `mm/heap.c` is what all of that unblocks:
  `kalloc` starts the heap at a 64KB mapping and grows it a chunk at a
  time (`alloc_frame` + `map_page`, at a dedicated virtual base well
  clear of both the static identity map and the `map` command's demo
  page) once the free list runs dry, up to a 16MB cap - not the fixed
  `.bss` arena earlier milestones used. `sched/task.c` is a fixed table
  of kernel tasks, each with its own `kalloc`'d stack; `create_task()`
  hand-builds a new task's initial stack to look exactly like what
  `switch_context()` would have left behind, with the task's entry point
  as a fake "return address," so the first switch into it lands there via
  an ordinary `ret`. `yield()` round-robins to the next task - called
  voluntarily by cooperative tasks, and *also* called from inside
  `isr/isr.c`'s timer handler now, which is what makes this preemptive:
  since `interrupt_handler` is just an ordinary nested C call on whichever
  task's stack the CPU happened to interrupt, calling `yield()` from
  inside it suspends that call exactly like a voluntary `yield()` would -
  the timer's full trap frame (pushed by `interrupts.s` below it on the
  stack) rides along for free and gets `iretq`'d correctly whenever the
  ring cascades back around to it. `sleep_ticks(ticks)` adds real blocking
  on top (named `sleep_ticks`, not `sleep`, to avoid any name collision
  with the POSIX libc function of that name even though nothing here
  links libc): a task marks itself `blocked` with a wake tick and yields
  away; `yield()`'s task-selection scan skips blocked tasks (waking one up
  the moment its wake tick arrives) instead of always picking "whoever's
  next" - task 0 (the shell) never blocks itself, which is what
  guarantees the scan always finds something runnable. `syscall/
  syscall.c`'s `syscall_dispatch(num, arg1, arg2, arg3)` is what
  `boot/interrupts.s`'s `isr_syscall` calls for every `int 0x80` - the
  calling convention (`rax` = number in / return value out, `rdi`/`rsi`/
  `rdx` = up to three arguments) is this kernel's own, since going
  through a software interrupt gate rather than the `SYSCALL`/`SYSRET`
  instruction pair means there's no fixed convention to inherit.
  **Milestone 12** added
  `mm/paging.c`'s `clone_address_space()`: a fresh PML4 whose entry 0
  points at a fresh PDPT that shares PDPT[0] (the static identity map)
  and PDPT[1] (heap/dynamic-demo region) with the kernel's own PDPT -
  literally the same physical sub-tables, so kernel code/stack/heap stay
  reachable unchanged - while everything from PDPT[2] up (virtual
  addresses `>= 0x80000000`) starts out not-present, private to whichever
  task gets that PML4. `map_page` got split into `map_page_in(pml4_phys, ...)`
  (walks an *explicit* address space - safe from any task's context,
  since every page-table frame `alloc_frame()` hands out lives inside the
  shared low-1GB identity map regardless of which CR3 is active) plus a
  `map_page(...)` convenience wrapper for the kernel's own space, and a new
  read-only `translate_in(pml4_phys, vaddr)` walk for verifying which
  physical frame a given address space's mapping actually points at.
  `sched/task.c`'s `task` struct gained a `cr3` field; `create_task()`
  keeps giving new tasks the kernel's own space unchanged, while
  `create_isolated_task()` calls `clone_address_space()` first. `yield()`
  loads the incoming task's `cr3` right before `switch_context()` - safe
  even while still running on the outgoing task's stack for that one
  instruction, since PDPT[0]/[1] are identical across every address
  space. Two demo tasks, `proc_a_entry`/`proc_b_entry`, both map the
  *same* virtual address (`0x80000000`) in their own private space to
  their own physical frame and write a different constant there - proving
  real isolation, not just separate stacks. **Milestone 13** added
  `proc/process.c`'s `spawn_process()`, wiring milestones 11 and 12
  together into a real loader: `proc/testprog.s` is a hand-assembled,
  position-independent ring3 program - not a compiled function, an opaque
  byte blob - that `spawn_process()` clones a private address space for,
  maps and copies page by page, and schedules as a genuine task via
  `create_task_with_cr3()`, whose one-line kernel-mode entry
  (`process_entry_trampoline()`) calls `run_ring3_test()` and never
  returns to kernel mode again. This is the first ring3 code in this
  kernel that runs preemptible, coexisting with ordinary tasks under the
  same round-robin scheduler, rather than milestone 11's one-shot demo
  (`cli`'d around the whole thing, now retired - see below). Doing this
  surfaced a real bug: the retired demo and this new mechanism both used
  the *same* `TSS.RSP0` stack for ring3->ring0 transitions, safe only as
  long as at most one such transition could ever be "in flight"
  (suspended, not yet resumed) at a time - true for the demo alone, false
  the moment a real preemptible ring3 task could also be mid-suspension
  when the demo fired. Fixed by retiring the demo entirely rather than
  coordinating two incompatible mechanisms - see `syscall/syscall.c`'s
  comment for the full diagnosis. **Milestone 14** added `proc/
  object.c`: a kernel-wide `kernel_object` table plus a *separate*
  per-process handle table (`g_handle_tables[4][8]`, a genuine
  two-dimensional C array - process P's handle H lives at
  `g_handle_tables[P][H]`), the NT-style piece of the long-term plan.
  `resolve_handle(process_index, handle)` (removed in milestone 27 - see
  below - once every caller also needed the handle's `rights` field, not
  just its object index, and inlined the same bounds/existence check
  directly) was the one place a small ring3-supplied integer got turned
  into an object index - bounds-checked and existence-checked, never
  trusted as a raw array index. `spawn_process()` gives every new process
  a handle to itself for free, guaranteed to land in slot 0 (a fresh
  handle table is empty, so the first allocation into it always takes
  slot 0) - "handle 0 = myself," no syscall needed just to discover it.
  `sched/task.c`'s `task` gained a `process_index` field (-1 for plain
  kernel tasks) so `syscall_dispatch` can find *whose* handle table a
  syscall's handle argument should be resolved against. New syscall
  number 3 (query handle) returns a process object's `task_index` -
  arbitrary but real ground truth, independently checkable against the
  `ps` shell command's own output - or the same `-1` sentinel any invalid
  handle produces. **Milestone 15** added `proc/channel.c`: a `channel`
  is a single-slot mailbox (one `u64` message) - `channel_send()` is
  non-blocking (fails outright if the mailbox is already full, rather
  than overwriting an unread message or blocking the sender too - a
  second hard problem, deliberately not tackled here). The one genuinely
  new mechanism is `sched/task.c`'s `channel_receive()`: it blocks the
  calling task exactly the way `sleep_ticks()` already does - marks
  itself blocked, this time with the new `waiting_channel` field set
  instead of a `wake_tick`, and yields away. `yield()`'s blocked-task
  scan gained a second wake condition alongside the tick check
  (`channel_has_message(waiting_channel)`) - the exact generalization the
  roadmap called for, reusing milestone 10's blocking mechanism for IPC
  rather than inventing a second one next to it. A real bug surfaced
  immediately: `g_tasks[8]` was already fully subscribed by task 0 +
  task1-4 + proc_a/proc_b + the one spawned ring3 process, so the two new
  demo tasks silently failed to create (`create_task_with_cr3` returning
  `false`, unchecked) - fixed by growing the table to 16 with headroom,
  not just enough for today. **Milestone 16** added `disk/ata.c`: a
  legacy ATA PIO driver talking directly to the classic ISA IDE ports
  (`0x1F0`-`0x1F7`, primary bus) - the first real storage I/O this
  kernel has ever done, same hand-rolled direct-port-I/O style as VGA/
  keyboard/PIT before it. `drivers/io.c` gained `outw`/`inw` (16-bit
  port I/O) since the ATA data port genuinely transfers a sector two
  bytes at a time, not one - every earlier port-I/O user only ever
  needed 8 bits. Polling, not interrupt-driven (one fewer moving part to
  prove the basic path works, same reasoning cooperative scheduling came
  before preemptive), with a *bounded* wait instead of a bare
  `while (busy) {}` - a missing/misconfigured drive fails cleanly after
  ~1,000,000 spins instead of hanging the kernel forever on hardware
  that isn't there. `make disk` (via `./build.sh disk`) creates a small
  (1MB, 2048-sector) raw disk image with a known ASCII signature at LBA 1
  and zeros everywhere else - not a filesystem yet (that's milestone
  17+), just known bytes at known addresses so read/write have something
  real to check themselves against. **Milestone 17** added
  `disk/minifs.c`: MiniFS, a minimal custom filesystem built directly on
  `ata_read_sector`/`ata_write_sector` - a fixed-layout superblock (LBA
  500) + a one-sector, 16-entry flat directory (LBA 501, each 32-byte
  `dir_entry` exactly filling the sector) + a contiguous data region (LBA
  502+), chosen specifically clear of milestone 16's own `disk`/
  `diskwrite` test LBAs so neither regressed. `fs_write_file()`
  recomputes the next free LBA by scanning existing directory entries
  each time rather than maintaining a persistent free list - the same
  "prove the mechanism first" scoping that put a bump allocator before
  the heap's free list. `sizeof`'s struct-layout guarantees did real work
  here: `dir_entry`'s `char name[20]` plus two `u32`s plus a `bool`,
  naturally padded to 32 bytes, is *exactly* what makes 16 entries fill
  one 512-byte sector precisely - not a coincidence, a deliberate size
  choice. Verified in QEMU: `mkfs` then two `mkfile`/`cat` round trips,
  each creating a file whose name and content both embed a running index
  (`file0.mfs`/`file1.mfs`, distinct content each) - `cat`-ing each one
  back showed the *correct, distinct* content for both (not the first
  file's content leaking into the second), and `ls` listed both with
  matching sizes and a superblock `file_count` that agreed with the
  actual directory contents. Independently confirmed on the **host** side
  afterward: reading `disk.img` directly at the superblock, directory,
  and both files' data LBAs showed the exact same bytes the kernel
  reported - genuine persistence to the backing store, not just something
  the kernel believed happened. **Milestone 18** added `disk/vfs.c`
  (routing) and `disk/devfs.c` (a second backend): a basic namespace
  (`/system`, `/devices`) so `vfs_read`/`vfs_write` can take a real path,
  find which mount prefix it falls under, strip it, and dispatch to
  whichever backend owns that mount - a `backend` tag field plus if/else,
  the same dispatch style `proc/object.c`'s `kernel_object.type` already
  established, not function pointers (C obviously supports real function
  pointers with no caveats MiniC's codegen had - this is now purely
  "hasn't been worth the refactor yet," see Known limitations).
  `disk/devfs.c`'s `/devices/ticks` reflects `g_tick_count` live,
  composed into the caller's buffer on the spot - nothing touches disk
  for it at all, which is the actual point: the identical `vfs_read()`
  call reaches two completely different mechanisms depending only on the
  path prefix, not a renamed MiniFS API. Needed two typeable characters
  the shell never had before (`/` and `.`, both purely kernel-generated
  in every earlier command's own output, never typed at the keyboard) -
  `drivers/keyboard.c`'s scancode table gained both. Verified in QEMU:
  `vfscat /system/file0.mfs` (routes to MiniFS, matches the same content
  `mkfile`/`cat` already proved) and `vfscat /devices/ticks` (routes to
  devfs, a live hex tick count, no disk touched) back to back - same
  function, two mechanisms. `vfswrite` writes a fixed file through
  `vfs_write()` rather than `fs_write_file()` directly; `vfscat`-ing it
  back, and separately running MiniFS's own `ls` (which has no idea the
  file arrived via VFS) both confirmed it landed in the exact same
  underlying MiniFS directory - the two layers genuinely share one
  filesystem, not parallel storage. **Milestone 19** made "the shell
  launches a program" genuinely real: `proc/process.c`'s
  `spawn_process_from_path(path, load_vaddr, stack_vaddr)` calls
  `vfs_read()` into a scratch buffer, then hands that range to
  `spawn_process()` completely unchanged - loading from disk turned out
  to be nothing more than "get the bytes into RAM first," reusing the
  whole milestone-13 loader as-is rather than needing a second one. The
  new `install` shell command writes the kernel's own compiled-in test
  program out to a real MiniFS file (`/system/testprog.bin`) through
  `vfs_write()`, simulating a program actually being installed on disk;
  `spawn` reads it back and launches a brand-new, independent instance
  from those bytes. This is the first time this kernel has ever run
  *two* ring3-capable processes at once (the milestone-13 boot-time one,
  still spinning, plus the newly spawned one) - which immediately
  reproduced the exact `TSS.RSP0` collision README's Known Limitations
  had been flagging as a prerequisite since milestone 13: a real GPF the
  moment both existed together, the second process's own syscalls
  corrupting the first's still-pending suspended state on the one shared
  RSP0 stack. Fixed for real this time (not deferred again): `sched/
  task.c`'s `task` gained a `kernel_stack_top` field - each task's own
  already-`kalloc`'d stack, otherwise abandoned the moment
  `run_ring3_test()` iretqs into ring3 and never returns through it,
  reused as that task's *private* RSP0 target rather than allocating a
  separate stack just for this. `yield()` now calls `mm/paging.c`'s new
  `set_tss_rsp0()` for the incoming task whenever it's ring3-capable,
  right alongside the existing `load_cr3()` call - the exact fix the
  earlier postmortems called for. Verified in QEMU: `spawn` completes
  cleanly, the new process's own `handle 0 (self) -> task_index` syscall
  reads back a *different* task index than the original process's, `ps`
  (extended from showing only process 0 to looping over every real
  process) lists both with distinct `cr3` values, and the system stays
  stable through an extended regression pass with both running
  concurrently. `shell/shell.c` is the interactive shell (`help`/
  `clear`/`ticks`/`alloc`/`bigalloc`/`free`/`free <addr>`/`mem`/`reset`/
  `frame`/`unframe`/`frames`/`map`/`tasks`/`procs`/`ps`/`objs`/`chan`/
  `send`/`disk`/`diskwrite`/`mkfs`/`mkfile`/`cat`/`ls`/`vfscat <path>`/
  `vfswrite`/`install`/`spawn`/`ring3go`/`ring3fault`/`ring3nx`/`pci`/
  `nic`/`arp`/`ping`/`dns`/`echo <text>`) built on `drivers/keyboard.c`'s
  line buffer.
- **`boot/linker.ld`** - places the multiboot header + code at the
  conventional 1MB load address multiboot expects.

## Building and running

Needs `qemu-system-x86_64` (`sudo apt install qemu-system-x86` on
Debian/Ubuntu/WSL) and a real GCC toolchain (`gcc`/`as`/`ld`/`objcopy` -
developed against gcc 15, any recent GCC works). No sibling checkout of
anything else is needed - the kernel used to build against a `minicc`
compiler from a separate `compiler/` repo; since the C rewrite, it's
just a normal freestanding-C project.

```bash
./build.sh          # runs `make`: compiles every .c, assembles every .s, links kernel.elf
./build.sh run       # also boots it in QEMU (curses display, in-terminal), with a disk attached
./build.sh iso       # also packages a GRUB-bootable minic-os.iso
./build.sh disk      # (re)builds disk.img - a 1MB test disk image, gitignored
```

`build.sh` is a thin wrapper over a real `Makefile` - every `.c` file compiles to its own object with real incremental rebuilds (`gcc -S` to assembly, a `.code64` directive prepended, `as --32` to an ELF32 object - see `CLAUDE.md` for why). `./build.sh run` builds `disk.img` automatically if it isn't already there (a fixed, regenerated-from-scratch test fixture, not real data - see the ATA driver's writeup below) and attaches it via QEMU's `-drive`. Booting and every command *except* `disk`/`diskwrite` work identically with or without it.

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
2. Give it a small amount of RAM (128MB+ is plenty). A virtual hard disk
   is optional - the kernel boots fine without one, only the `disk`/
   `diskwrite` shell commands (milestone 16+) need one actually attached.
3. Settings → Storage → attach `minic-os.iso` as the optical drive, and
   (optionally, for disk commands) attach `disk.img` (`./build.sh disk`)
   as a plain IDE hard disk on the same controller.
4. Start the VM - GRUB's menu appears, boots straight into the kernel.

VMware and real hardware (via a USB stick written with `dd` or similar)
should work the same way, unverified so far.

The build assembles every object as a **32-bit ELF container** even
though `kmain.c`'s code (and `boot.s`'s post-transition half) runs in
64-bit long mode - multiboot1's loader (and QEMU's/GRUB's implementation
of it) only understands a 32-bit ELF *container*. `.code32`/`.code64` are
per-region encoding directives, independent of that container format, so
the Makefile compiles each `.c` file to assembly (`gcc -S`) and prepends
a `.code64` directive before handing it to `as --32` (gcc only ever
targets hosted 64-bit ELF on its own, so it doesn't emit one itself) -
see `CLAUDE.md` for the two real wrinkles C introduces here that MiniC's
own simpler codegen never hit (`-fPIC`/`-fvisibility=hidden`).

To check output without a display, redirect the serial port to a file
(add `-drive file=disk.img,format=raw,if=ide` too if testing `disk`/
`diskwrite`):

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
handle 0 (self) -> task_index 0x7
handle 99 (invalid) -> 0xffffffffffffffff
File.write() wrote 0x36
File.read() got back 0x36
hello from ring3, via a real File.write() method call!0
POSIX read() 1: 0
POSIX 0
POSIX read() 2: 0
shim works!0
Channel.open() ok=0x1
unauthorized Channel.send() succeeded=0x0
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
proc_a: 0xaaaaaaaa @phys 0x426000 proc_b: 0xbbbbbbbb @phys 0x429000
> ps
processes: 0x1 proc0 task=0x7 cr3=0x426000
> objs
objects: 0x2 obj0 type=0x1 data_index=0x0
> chan
receiver got: 0x0 value=0x0
> chan
receiver got: 0x0 value=0x0
> send
sent 0xc0ffee1234
> chan
receiver got: 0x1 value=0xc0ffee1234
> pci
pci devices: 0x6 0:0.0 vendor=0x8086 device=0x1237 class=0x6 subclass=0x0 0:1.0 vendor=0x8086 device=0x7000 class=0x6 subclass=0x1 0:1.1 vendor=0x8086 device=0x7010 class=0x1 subclass=0x1 0:1.3 vendor=0x8086 device=0x7113 class=0x6 subclass=0x80 0:2.0 vendor=0x1234 device=0x1111 class=0x3 subclass=0x0 0:3.0 vendor=0x8086 device=0x100e class=0x2 subclass=0x0
> nic
e1000 mac=52:54:0:12:34:56 link_up=0x1
> arp
resolve gateway ok=0x1 mac=52:55:a:0:2:2 elapsed_ticks=0x64 cached_ok=0x1 cached_elapsed_ticks=0x0 resolve_dns_proxy_ok=0x1 dns_mac=52:55:a:0:2:3 resolve_unreachable_ok=0x0
> ping
ping gateway ok=0x1 elapsed_ticks=0x64
> dns
dns query ok=0x1 elapsed_ticks=0x74
> disk
sector 1: MiniC ATA PIO driver - milestone 16 signature sector
> diskwrite
write+readback verified, 512/512 bytes match
> mkfs
filesystem formatted
> mkfile
created file0.mfs
> cat
Hello from MiniFS, this is file #0
> mkfile
created file1.mfs
> cat
Hello from MiniFS, this is file #1
> ls
file_count: 0x2  file0.mfs 0x23  file1.mfs 0x23
> vfscat /system/file0.mfs
Hello from MiniFS, this is file #0
> vfscat /devices/ticks
ticks: 0x3d8a
> vfswrite
wrote /system/vfsdemo.mfs via VFS
> vfscat /system/vfsdemo.mfs
This file was written through the VFS layer, not MiniFS directly.
> install
installed /system/testprog.bin, 0x20c0 bytes
> spawn
spawned process 0x1
> ps
processes: 0x2 proc0 task=0x7 cr3=0x426000 proc1 task=0x9 cr3=0x444000
> ring3go
sent ring3 spawn trigger
```

That last `ring3go` line wakes the boot-time ring3 process's own blocking
`channel.receive()` call - a completely different code path from
`install`/`spawn` above, which the *shell* (kernel mode) drives directly.
The ring3 process's own reaction (printed to serial, not shown by any
shell command) looks like this on a fresh boot (`mkfs`, `install`,
`ring3go`, in that order, no `spawn` needed - the ring3 process spawns
its own child this time):

```
Channel.receive() got trigger 0x1
hello from a LOADED process! 0xc0ffee
Process.spawn() launched task_index 0x9
hello from a LOADED process! 0xc0ffee
ProcessHandle.open(rights=0) ok=0x1
unauthorized ProcessHandle.query() got 0xffffffffffffffff
ProcessHandle.open(RIGHT_QUERY) ok=0x1
handle 0 (self) -> task_index 0x9
authorized ProcessHandle.query() got task_index 0x9
handle 99 (invalid) -> 0xffffffffffffffff
File.write() wrote 0x36
File.read() got back 0x36
hello from ring3, via a real File.write() method call!0
POSIX read() 1: 0
POSIX 0
POSIX read() 2: 0
shim works!0
Channel.open() ok=0x1
unauthorized Channel.send() succeeded=0x0
```

Everything from the first `hello from a LOADED process!` onward is the
*spawned child* - the exact same `ring3prog.mc` blob, running from
`_start()` again from scratch in its own freshly cloned address space,
with its own distinct `task_index` (`0x9`, not the parent's `0x7`) and its
own independent handle table - except now it's genuinely INTERLEAVED
with the parent's own remaining code (milestone 27's `process_handle`
demo, still running in the parent's own `_start()` after its
`process.spawn()` call returns), not simply "runs after." The exact
interleave point isn't guaranteed run to run - a real preemptive
scheduler, same one milestone 9 proved - which is why the child's own
first print lands *before* the parent's own `Process.spawn() launched
task_index 0x9` line in this particular capture, not after, unlike some
earlier transcripts. What's NOT allowed to vary is which VALUES each
side reports, and they don't: the child's own `handle 0 (self) ->
task_index 0x9` is still exactly its own task_index, and the parent's
`process_handle` lines (see below) are unaffected by whatever the child
happens to be doing concurrently. The child reaches its own
`channel.receive()` call too, but the channel's single-slot mailbox is
already empty again (the parent's `receive()` consumed the one message
`ring3go` sent), so it just blocks there - never reaching its own
`process.spawn()` call, which is what stops this from spawning a second,
third, fourth generation automatically.

`ProcessHandle.open(rights=0) ok=0x1` through `authorized
ProcessHandle.query() got task_index 0x9` are milestone 27's proof, run by
the *parent* right after its own `process.spawn()` call, using the
child's just-returned `task_index` (`0x9`) as the target. `open(rights=0)`
deliberately requests NO rights and still gets back a real, valid handle
(`ok=0x1`) - proving a handle's existence and its rights are two
separate things now, same distinction milestone 25 established for
`channel`. The next line, `unauthorized ProcessHandle.query() got
0xffffffffffffffff`, is the actual point: querying through that
rights-less handle is correctly rejected, the same `-1` sentinel every
other invalid-handle path in this kernel already uses. The second
`open()`, requesting `RIGHT_QUERY` this time, succeeds and its
`.query()` returns `0x9` - the exact task_index `process.spawn()` already
reported two lines earlier, an independent cross-check that this is
real ground truth, not a hardcoded echo of what was requested. The
`handle 0 (self) -> task_index 0x9` line in between (the child's own,
pre-existing self-query, unaffected by any of this) is the built-in
regression check: `RIGHT_QUERY` was already being granted to every
self-handle since milestone 14, just never *verified* until this
milestone - if enforcing the check had broken something, this exact
line would have shown `-1` instead of `0x9`.

Milestone 26's proof is a separate, dedicated session - deliberately not
folded into the transcript above, since it ends by halting the kernel on
purpose (a fresh boot, `ring3fault` typed with no `mkfs`/`install` first -
this test only needs the boot-time ring3 process and the existing
`g_ring3_channel_demo` channel, nothing disk-backed):

```
> ring3fault
sent ring3 forbidden-write trigger - expect a page fault
Channel.receive() got trigger 0x2
attempting forbidden ring3 write to 0x100000
page fault at 0x100000, error_code=0x7, halting
```

That's the whole point: `attempting forbidden ring3 write to 0x100000` is
printed - `proc/ring3prog.mc`'s `_start()` really did reach the `*forbidden
= 0xDEADBEEF;` line and attempt the write - and then nothing else from
that process ever prints again. Specifically, the line right after it in
the source, `"forbidden write succeeded (BUG!) at 0x..."`, never appears -
the CPU faulted on the write itself, before that next syscall could even
be staged. `page fault at 0x100000, error_code=0x7, halting` is
`isr/isr.mc`'s existing vector-14 handler independently confirming the
exact faulting address (read from CR2, not something the ring3 process
reported about itself) is `0x100000` - the kernel's own multiboot load
address, definitely present in the shared identity map, definitely never
something this process mapped for itself. Before the milestone 26 fix,
this same command would have printed the "(BUG!)" line and kept running
(the write silently landing in live kernel memory) instead of faulting -
the *absence* of that line, replaced by a page fault at exactly the
address that was written to, is what makes this a genuine negative-space
proof rather than a topology change nobody actually exercised.
`error_code=0x7` (present + write + user, milestone 28's addition to this
handler) confirms it specifically as a WRITE violation - worth comparing
directly against milestone 28's own `ring3nx` proof below, which faults
with a *different* error code for a different reason.

Milestone 28's proof is likewise a separate, dedicated session (same
one-shot, kernel-halting caveat) - a fresh boot, `ring3nx` typed with no
`mkfs`/`install` needed, same as `ring3fault`:

```
> ring3nx
sent ring3 stack-execution trigger - expect a page fault
Channel.receive() got trigger 0x3
attempting to execute ring3 stack byte at 0x80020000
page fault at 0x80020000, error_code=0x15, halting
```

`proc/ring3prog.mc`'s `_start()` writes a real `0xC3` (`ret`) opcode to
`0x80020000` - the well-known base of this process's own user stack -
then attempts `call rax` on that exact address via a raw `asm(...)`
block. If `PAGE_NX` (`mm/paging.mc`) weren't actually being enforced on
the stack mapping, this would be entirely harmless (the `ret` would just
pop the return address `call` pushed and jump straight back) and the
`"stack execution succeeded (BUG!)"` line right after would print. It
never does. Instead, `page fault at 0x80020000, error_code=0x15, halting`
fires - and `0x15` (binary `10101`: present + user + **bit 4, instruction
fetch**) is the actual proof, not just "a page fault happened somewhere."
Bit 4 is the CPU's own signal that this specific access was an
*instruction fetch*, set by hardware only when EFER.NXE is live and the
translated entry's NX bit is set - directly comparable against
`ring3fault`'s own `error_code=0x7` (bit 4 clear, a write violation)
captured moments above. Two different deliberate violations, two
different, independently-checkable error-code signatures - exactly the
kind of concrete assertion this project has held itself to since
milestone 1, not "didn't crash" or even just "a fault happened."

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
re-`map_page`-ing them (which would leak a frame per byte remapped).

That `pci` result is milestone 29's proof, and its strength comes from
being checkable against something this kernel had no part in choosing:
QEMU's own default machine model. `0x8086:0x1237` (Intel 82441FX PMC,
the host bridge), `0x8086:0x7000`/`0x7010`/`0x7113` (Intel PIIX3 ISA
bridge / IDE controller / PIIX4 ACPI bridge - `0x7010` is literally the
same IDE controller `disk`/`diskwrite` above already talk to, just now
independently *discovered* rather than assumed), `0x1234:0x1111`
(the Bochs/QEMU standard VGA device), and `0x8086:0x100e` (an Intel
82540EM - a real e1000 gigabit NIC, found with zero extra QEMU flags,
future networking milestones' target device) - every single one of
these is a publicly documented, real PCI vendor:device ID pair, not
something this driver could fake its way into looking right. Getting
a wrong answer here (a garbage vendor ID, a device that doesn't exist,
a missing one) would be immediately, independently checkable against
public PCI ID databases, the same "wrong answer is obviously wrong"
property this project has looked for in every milestone's own
verification since milestone 1.

That `nic` result is milestone 30's proof, and it's independently
checkable the same way: `52:54:00:12:34:56` is QEMU's own well-known,
publicly documented default NIC MAC address (`52:54:00` is QEMU's
locally-administered OUI-style prefix) - reading it back correctly
proves this driver genuinely mapped the e1000's real MMIO register file
(not RAM - the first device-register mapping this kernel has ever done,
via `mm/paging.mc`'s ordinary `map_page()`, since the mapping mechanism
itself doesn't care whether the target physical address is RAM or a
device's registers) and read real hardware state out of it (`RAL0`/
`RAH0`, pre-loaded by QEMU's own emulated EEPROM), not a fabricated
number. `link_up=0x1` is the second, separate confirmation - real
hardware state a broken or unmapped driver couldn't have produced
either. Getting either of these wrong (a zeroed or garbage MAC, a
stuck-low link bit) would have been immediately, independently
checkable against a publicly known value, not just "the command didn't
crash."

That `arp` result is milestone 32's proof, exercising the real
`net/arp.mc` resolver (`arp_resolve()`) instead of milestone 31's own
hardcoded one-shot flow - four separate, independently checkable claims
in one command. `resolve gateway ok=0x1 mac=52:55:0a:00:02:02` is a
genuine external round trip, the same shape milestone 31 first proved,
now through the real API: QEMU's SLIRP backend, running entirely outside
this kernel, received a real ARP request and replied with its own
well-known, publicly documented default gateway MAC. `elapsed_ticks=0x64`
(100 ticks) is real, non-trivial wall-clock time - a genuine network
round trip actually took a measurable amount of time. `cached_ok=0x1
cached_elapsed_ticks=0x0` is the real proof of the cache: resolving the
identical address a SECOND time returns instantly, zero ticks elapsed,
because it never sends a packet at all - a concrete, timing-based
behavioral difference between a cache hit and a cache miss, not just "the
second call returned the same answer as the first" (which a broken cache
that just re-resolved every time would also produce).
`resolve_dns_proxy_ok=0x1 dns_mac=52:55:0a:00:02:03` resolves a SECOND,
different real address (QEMU SLIRP's own built-in DNS proxy) - the MAC
that comes back is genuinely different from the gateway's, and follows
the exact same publicly-documented SLIRP convention (embedding the IP's
last three octets into the MAC), proving the resolver generalizes past
the one fixed address milestone 31 ever exercised, not something
special-cased for the gateway alone. `resolve_unreachable_ok=0x0` is the
negative-space proof: asking to resolve an address nothing ever answers
for (`10.0.2.99`) correctly returns false once the resolver's own real
tick-bounded timeout expires, rather than hanging forever or returning
garbage - a real, honest failure result for a real failure case.

That `ping` result is milestone 33's proof: a genuine ICMP echo round
trip through the full stack built so far - ARP resolution, real IPv4
header construction, a real checksum, and a real Ethernet send/receive,
all in one. `ok=0x1` means `net/icmp.mc`'s `icmp_ping()` got back a reply
that matched on EVERY real, independently meaningful field: the right
EtherType, the right IP protocol number, the right source IP, the right
ICMP type (echo reply, not some other ICMP message), AND the exact
identifier/sequence number this specific request sent - not just "some
ICMP traffic arrived." A wrong IP header (a bad checksum, a malformed
field) would most likely have gotten the packet silently dropped by
QEMU's real host-level network stack rather than answered at all, so the
reply arriving *at all* is itself strong indirect evidence the header was
built correctly - the same "external validation beats self-validation"
property every proof in this networking phase has looked for.
`elapsed_ticks=0x64` matches the same real, non-trivial round-trip time
`arp` measured moments earlier - consistent, not coincidental, real
network latency. This is also the first milestone in this whole project
to give the kernel a real, explicit "my IP address" (`net/ip.mc`'s
`g_my_ip`) - previously a bare literal duplicated inside `arp.mc`'s own
request-building code, now one shared, named value both files use.

That `dns` result is milestone 34's proof: `net/udp.mc`'s real UDP layer
(a real pseudo-header checksum, correctly binding the datagram to its
own source/destination addresses, not just its own bytes), verified with
a genuine DNS query to QEMU's built-in SLIRP DNS proxy (`10.0.2.3`, the
same address `arp` already resolved back in milestone 32). `ok=0x1`
means `net/dns.mc`'s `dns_query()` got back a response that matched the
exact transaction ID this query sent, had the response (QR) bit set, and
reported at least one real answer record - meaning SLIRP's proxy
genuinely forwarded the query to a real upstream resolver and got a real
answer back, not something this kernel could produce on its own.
`elapsed_ticks=0x74` (116 ticks) is itself a meaningful, sensible detail:
noticeably *higher* than `ping`'s own `elapsed_ticks=0x64` - real,
consistent with DNS resolution genuinely taking one more hop than ICMP
echo (SLIRP forwards the query out to a real resolver and waits for
*its* reply, rather than answering directly the way it does for ping) -
exactly the kind of small, correct-shaped detail that would be hard to
fake by accident.

The two `hello from a LOADED process!` lines, printed automatically
before the shell even shows its first prompt, are the milestone 13
proof: `proc/ring3prog.mc` (real, `minicc`-compiled MiniC as of
milestone 21 - hand-assembled machine code the kernel never compiled
before that) gets copied by `spawn_process()` into a freshly cloned
private address space and jumped into via `run_ring3_test()` - two real
`int 0x80` syscalls from inside that loaded, executing code, printing a
message that lives *inside the blob itself*, not a kernel string. Milestone
11's old `ring3` shell command (`cs=0x1b` readback proving CPL 3) is
retired - `spawn_process()`'s task takes over that proof and more: it's
scheduled like any other task, interleaved with the boot sequence and
the rest of the demo tasks without any `cli` wrapping, and `ps`'s
`proc0 task=0x7 cr3=0x426000` confirms it's a real, queryable process
object, not just something that ran once and vanished.

The next three lines are the milestone 22 proof: `File.write() wrote
0x36` (54, the real byte count of the message literal) followed by
`File.read() got back 0x36` and the message printed back verbatim -
`proc/ring3prog.mc`'s `_start()` creates a real `file` struct
(`msg_file.path = "/system/ring3msg.txt"`) and calls
`msg_file.write(...)`/`msg_file.read(...)` - genuine method calls
(`obj.method()`, milestone 20's syntax) from inside a ring3 process,
wrapping the new vfs_read/vfs_write syscalls (numbers 4/5) rather than
raw syscall numbers. A rebooted VM reusing the same `disk.img` shows
`File.write() wrote 0xffffffffffffffff` instead (MiniFS's "fails
outright if the name already exists" rule, from milestone 17, firing
correctly against a file the *previous* boot's write already created) -
while `file.read()` still succeeds with the identical byte count and
content, since the file's real, persisted-to-disk content is what's
actually being read back either way. That failure-on-reboot is itself
independent proof the write reached real, persistent storage on the
first boot, not just something the ring3 process believed happened in
memory.

The four `POSIX ...`/`shim works!` lines are the milestone 24 proof.
`open("/system/posix.txt", 1)` starts a fresh in-memory buffer;
`write(wfd, "POSIX ", 6)` then `write(wfd, "shim works!", 11)` -
**two separate calls** - append to it; `close(wfd)` flushes the whole
17-byte buffer through `file.write()` in one shot. Then
`open("/system/posix.txt", 0)` loads that same 17 bytes back into a
fresh per-fd buffer; `read(rfd, buf1, 6)` returns exactly `"POSIX "`
(printed as `POSIX ` immediately followed by the call's own `0` hex
arg, i.e. `POSIX 0`) and `read(rfd, buf2, 11)` returns exactly
`"shim works!"` from the ADVANCED position - **two separate calls
returning two different, correct slices of the same buffer** is what
proves the position cursor genuinely tracks across calls, not just
replaying the whole thing every time. All of this - fd table,
position tracking, buffering - is pure ring3 MiniC; no new syscalls,
no kernel changes at all for this milestone.

`Channel.open() ok=0x1` and `unauthorized Channel.send() succeeded=0x0`
are the milestone 25 proof - and the second line is the one that
actually matters. `spawn_trigger.open(1)` (a new syscall, number 9)
turns the ring3-dedicated channel's raw index into a real handle,
resolved through this process's own handle table - `ok=0x1` just shows
the kernel granted *something*. The real test is what happens next:
`spawn_trigger.send(0xDEADBEEF)` through that exact same handle -
deliberately attempting an operation the kernel's own policy (`open()`
always grants `RIGHT_RECEIVE`, never `RIGHT_SEND`) never authorized.
`succeeded=0x0` (false) is the whole point: without real per-handle
rights enforcement, checked at the syscall boundary before the
underlying channel is ever touched, this call would have silently
succeeded - a receive-only handle would have been able to send anyway,
exactly the "any valid handle can do anything the object supports" gap
Phase IX exists to close. The very next line,
`Channel.receive() got trigger 0x1`, proves the fix isn't overzealous
either - the *authorized* operation on that same handle still works
correctly, unaffected.

`Channel.receive() got trigger 0x1` and everything after it is the
milestone 23 proof - and the riskiest new mechanism this milestone
added: `spawn_trigger.receive()` is a real, *blocking* ring3 syscall
(number 8), the first one this kernel has ever had. Every syscall
before this (print, handle-query, vfs_read/vfs_write) returned
immediately; this one suspends the calling task exactly the way
`channel_receive()` already did for a plain kernel task
(`proc_receiver_entry`, milestone 15) - `yield()`/`switch_context()`
underneath, unchanged - except now that suspension happens *while the
task is mid-syscall*, with its `isr_syscall`/`syscall_dispatch`/
`channel_receive` call frames still resident on its own kernel stack.
That the shell stayed fully responsive (`chan` still worked, showing
the *other*, unrelated milestone-15 channel) while this task sat
blocked for an arbitrary number of other commands, then correctly
resumed and printed the right value the instant `ring3go` ran, is what
proves this actually works - a stale/corrupted resume here would have
looked like a hang or a crash, not a wrong number. `Process.spawn()
launched task_index 0x9` is the second half: a real ring3 syscall
(number 6) reaching `spawn_process_from_path()` - the exact same function
the shell's own `spawn` command already calls, just reached from a
completely different, ring3-initiated path this time. `ps` afterward
shows two real, independent processes with different `cr3` values,
same as milestone 19's original proof, now demonstrated with the
*second* process created by the *first*, not by the shell.

The `handle 0 (self) -> task_index 0x7` / `handle 99 (invalid) ->
0xffffffffffffffff` lines are the milestone 14 proof, and they're a
matched positive/negative pair on purpose. The first shows the loaded
ring3 process resolving its *own* well-known handle (0) all the way
through to a piece of kernel ground truth (`task_index`) - `0x7` is
exactly what `ps` independently reports for this same process later in
the same session, which is the actual point: two different paths (a
ring3 syscall's handle lookup, and the shell's direct table read) landed
on the identical number, meaning the handle genuinely resolved through
`g_objects`/`g_processes` rather than being hardcoded or coincidental. The
second shows a handle number that was never allocated (99, past the
8-per-process limit *and* never assigned even if it weren't) coming back
as the same `-1` sentinel any invalid handle produces - not garbage, not
a crash - proving the bounds/existence check is real, not decorative. `objs`' `obj0 type=0x1 data_index=0x0` confirms
the object side directly: exactly one `kernel_object` exists, its type is
`OBJ_PROCESS` (`1`), and it points at `g_processes[0]` - the same process
`ps` and the ring3 self-handle both already agreed on.

The `chan`/`send`/`chan` sequence is the milestone 15 proof, and it's
deliberately operator-controlled rather than timed - QEMU/TCG's timer
runs at wildly varying effective rates across sessions (the same gotcha
`kernel-qemu-test` already documents for `g_tick_count` comparisons), so a
demo that raced a fixed `sleep()` duration against a fixed real-time
delay turned out unreliable during development, caught before it ever
shipped. The first two `chan` reads (`receiver got: 0x0 value=0x0`),
run with an arbitrary number of other commands in between, show the
receiver task genuinely still blocked on an empty channel for as long
as nobody sends anything - not a short timeout, not a no-op. `send`
calls `channel_send()` directly from the shell's own context (task 0,
the *kernel's* address space) - the instant it succeeds, the third
`chan` shows `receiver got: 0x1 value=0xc0ffee1234`: the exact value
just sent, and the receiver (a `create_isolated_task()` process with its
own private `cr3`, same as `proc_a`/`proc_b`) woke up on its own, without
ever being polled from the shell - `yield()`'s scan found
`channel_has_message()` true and cleared `blocked` the same way it already
does for an expired `sleep()`. The message genuinely crossed from one
isolated address space to another through the channel, not shared
memory.

The `disk`/`diskwrite` results are the milestone 16 proof, and like
milestone 15's, they're a deliberate two-part check rather than one.
`disk` reads LBA 1 and gets back the *exact* signature string
`build.sh disk` wrote there from the host side, before the kernel ever
booted - proving the driver can read real, independently-verifiable
content off the (emulated) disk, not a coincidence or a cached value
the kernel already had lying around. `diskwrite` then writes a distinct
512-byte pattern (`0x00 0x01 0x02 ... 0xFF` repeating) to a different
LBA and reads it straight back, reporting `512/512 bytes match` -
proving the write path independently of the read path (a bug that
happened to cancel out between the two would still show up as `0x00`
read back for whatever was written, not a byte-exact match on a
256-value repeating pattern). Checked independently a third way outside
the kernel entirely: reading `disk.img` directly on the host at LBA
100's byte offset after the test showed the identical pattern already
sitting there, confirming the write reached the actual backing file,
not just something the kernel believed had happened.

The `mkfs`/`mkfile`/`cat`/`ls` sequence is the milestone 17 proof, and
it's built the same three-part way milestone 16's was. Creating two
files back to back and `cat`-ing each one *immediately after* the next
`mkfile` proves the second file's write didn't clobber the first, and
that `cat` (which always reads freshly from disk, not a cached copy)
genuinely distinguishes between them - `file0.mfs` still reads back
`...file #0` even after `file1.mfs` exists. `ls`'s `file_count: 0x2`
matches the two entries it actually lists, proving the superblock's
metadata is maintained correctly, not just initialized once and
forgotten. Independently confirmed a third way, outside the kernel
entirely: reading `disk.img` directly on the host afterward showed the
`MFS1` magic and `file_count=2` at the superblock's LBA, both directory
entries at the expected offsets with the expected names/sizes/start
LBAs, and both files' exact text sitting at those start LBAs - the same
bytes the kernel reported, independently verified from outside it.

The `vfscat`/`vfswrite`/`vfscat` sequence is the milestone 18 proof.
`vfscat /system/file0.mfs` reading back the exact same content
`cat`/`mkfile` already proved a paragraph ago shows the VFS genuinely
reaches MiniFS through the new path-based routing, not a separate,
disconnected mechanism. `vfscat /devices/ticks` immediately after,
returning a live hex tick count instead of anything disk-related, is
the actual point of the whole milestone: the *identical* `vfs_read()`
call, given only a different path prefix, reached a completely
different backend - real dispatch, not a renamed API with one hardcoded
destination. `vfswrite` (which calls `vfs_write()`, never touching
`fs_write_file()` directly) followed by `vfscat` on that same path proves
the write side routes too, not just reads - and running plain `ls`
afterward (MiniFS's own directory listing, with no idea any of its
entries arrived via VFS) would show `vfsdemo.mfs` sitting right next to
`file0.mfs`/`file1.mfs` in the same directory, confirming the two
layers genuinely share one underlying filesystem rather than VFS being
a parallel, disconnected storage silo.

The `install`/`spawn`/`ps` sequence is the milestone 19 proof. `install`
writing `0x1eb8` (7864) bytes - the real, host-verifiable size of `proc/
ring3prog.mc`'s compiled-and-flattened blob (`0xd0`/208 bytes back when
this was `proc/testprog.s`'s hand-assembled version, pre-milestone-21;
`0x250`/592 right after milestone 21 first compiled it; it's simply
grown since, as milestones 22-24 added the `file`/`channel`/`process`/
POSIX-shim code this same blob now also contains - milestone 24 is also
where this size first crossed 4096 bytes, one page, which is exactly
what exposed two of that milestone's three real bugs, see below) -
confirms the
compiled-in program genuinely reached disk through
`vfs_write()`. `spawn` then reads it back
and launches a second process; the loaded program's own two greeting
prints appearing a second time in the log, followed by *its own* `handle
0 (self) -> task_index` syscall reading back a task index different from
the original boot-time process's, is what actually distinguishes "a
real second process" from "the first one ran again." `ps`'s two lines
with different `cr3` values (`0x426000` vs `0x444000`) confirms the same
thing from the kernel's own bookkeeping, independent of anything the
processes reported about themselves. None of this actually completed
cleanly the first time - the very first `spawn` attempt reproduced the
`TSS.RSP0` collision flagged as a known limitation since milestone 13
(a real GPF, not a hypothetical), fixed by giving every ring3-capable
task its own private RSP0 target instead of sharing one; the transcript
above is the *post-fix* run, verified stable across many repeated
`spawn`/`ps`/`procs` sequences afterward, including extended stress
testing specifically hunting for any recurrence of the collision this
fix was meant to close.

That `procs` result is the milestone 12 proof: `proc_a` and `proc_b` both
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
   an ordinary MiniC function and a freshly `map_page`'d stack are enough
   to prove the privilege-transition mechanism itself works. Verified in
   QEMU: two `int 0x80` round trips read back `cs=0x1B` from ring3 (CPL 3,
   the real ring3 code segment - not just "a syscall happened", which
   would succeed from any ring), and a `switch_context`-style one-shot
   exit trick gets cleanly back to the shell afterward, not a hang.~~
2. ~~**Per-process address spaces** (milestone 12) - `mm/paging.mc` gained
   `clone_address_space()`, building a *new* PML4 that shares the kernel's
   own PDPT[0]/PDPT[1] (identity map + heap) but leaves everything from
   PDPT[2] up (`vaddr >= 0x80000000`) private, populated on demand via a
   new `map_page_in(pml4_phys, ...)` that walks an explicit address space
   instead of always the global one. `sched/task.mc`'s `task` gained a
   `cr3` field; `yield()` loads it before every `switch_context()`.
   Verified in QEMU: two demo tasks (`proc_a_entry`/`proc_b_entry`) map the
   *identical* virtual address in their own private space to their own
   physical frame and each reads back its own distinct constant forever -
   critically, `translate_in()` shows that same virtual address resolving
   to two genuinely different physical addresses depending on which
   address space asks, not just "two different numbers came back."~~
3. ~~**A real `process` concept + a loader** (milestone 13) -
   `proc/process.mc`'s `spawn_process()` treats a byte range as an opaque
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
   the NT-style piece) - `proc/object.mc`'s `kernel_object` table plus a
   *separate* per-process handle table (a ring3-supplied handle integer
   is always bounds-checked and existence-checked before use, never
   trusted directly). Every process gets a
   handle to itself for free in the well-known slot 0. New syscall number
   3 resolves a handle within the *calling* process's own table.
   Verified in QEMU: a loaded process resolving its own handle 0 gets
   back its `task_index` (`0x7`), independently matching what the `ps`
   shell command reports for the same process moments later - two
   different paths landing on the identical number, not a coincidence -
   while an invalid handle (99, never allocated) comes back as the same
   `-1` sentinel every failure path uses, not garbage or a crash.~~
5. ~~**IPC channels** (milestone 15) between isolated processes -
   `proc/channel.mc`'s `channel` is a single-slot mailbox; `channel_send()`
   is non-blocking (fails if already full - a blocking send is a
   separate hard problem, not tackled here). `sched/task.mc`'s
   `channel_receive()` is the one genuinely new mechanism: it blocks the
   calling task exactly the way `sleep()` already did, just with
   `waiting_channel` set instead of a `wake_tick` - `yield()`'s
   blocked-task scan gained a second wake condition
   (`channel_has_message()`) alongside the tick check, reusing milestone
   10's blocking mechanism for IPC rather than inventing a second one,
   exactly as planned. Verified in QEMU: an isolated process blocked on
   `channel_receive()` since boot read back "still empty" across an
   arbitrary number of other shell commands, then woke up with the exact
   value the instant a `send` shell command (running in the kernel's own
   address space) delivered it - proving both a real, unbounded block and
   a real cross-address-space delivery, not shared memory. Along the way:
   a real bug (`g_tasks[8]` was already fully subscribed, so the two new
   demo tasks silently failed `create_task_with_cr3` - grown to `g_tasks[16]`
   with headroom) and two more real MiniC language gaps found and fixed
   in the `minic` repo (multi-dimensional array declarations, `const`),
   then used here to remove `proc/object.mc`'s manually-flattened handle
   table and only-by-convention-const globals.~~
6. ~~**Storage + VFS + a first filesystem**~~ - done, spanned three
   milestones:
   - ~~**Milestone 16: a legacy ATA PIO disk driver** - real sector-
     granular reads/writes against the classic ISA IDE ports
     (`0x1F0`-`0x1F7`), polling rather than interrupt-driven, with a
     *bounded* busy-wait (fails cleanly after ~1,000,000 spins instead
     of hanging forever if no drive is attached). `drivers/io.mc` gained
     `outw`/`inw` (16-bit port I/O) - the data port transfers a sector
     two bytes at a time, the first port-I/O user in this kernel that
     needed more than 8 bits. `build.sh disk` builds a small (1MB) test
     disk image with a known signature at a known LBA. Verified in QEMU
     three independent ways: reading back the exact host-written
     signature at LBA 1 (proves real reads), a 512-byte write-then-read
     round trip matching byte-for-byte at a different LBA (proves real
     writes, independently of the read path), and confirming that
     written pattern directly in the host's `disk.img` file afterward
     (proves the write reached the actual backing store, not just
     something the kernel believed).~~
   - ~~**Milestone 17: a minimal custom filesystem** ("MiniFS" - simpler
     than FAT32/ext2 for a first pass; those come later as additional
     VFS backends, the whole point of having a VFS layer at all).
     `disk/minifs.mc`: a fixed-layout superblock + one-sector 16-entry
     flat directory + a contiguous data region, all on a reserved LBA
     range clear of milestone 16's own test fixtures. `fs_write_file()`
     recomputes the next free LBA from the directory each call rather
     than maintaining a persistent free list - prove the mechanism
     first, the same reasoning behind every earlier "simplest thing that
     works" milestone in this kernel. Verified in QEMU: format, create
     two files back to back (name and content both embedding a running
     index), `cat` each one immediately after the *next* file was
     created (proving no cross-file clobbering, and that reads are
     always fresh from disk), `ls` reporting a superblock `file_count`
     that matches the real directory contents. Independently confirmed
     a third way, entirely outside the kernel: reading `disk.img`
     directly on the host afterward showed the exact same superblock,
     directory entries, and file contents the kernel reported.~~
   - ~~**Milestone 18: a VFS abstraction** above MiniFS, with a basic
     namespace (`/system`, `/devices`). `disk/vfs.mc`'s `Mount` table
     maps a path prefix to a backend tag; `vfs_read`/`vfs_write` find the
     matching mount, strip the prefix, and dispatch via tag + if/else
     (the same style `proc/object.mc`'s handle-table dispatch already
     used, not function pointers - untested in this kernel and
     unnecessary for what needed proving). `disk/devfs.mc` is the second
     backend, proving the abstraction is real: `/devices/ticks` reflects
     `g_tick_count` live, no disk touched at all. Needed the shell to
     finally type `/`/`.` (`drivers/keyboard.mc`'s scancode table gained
     both - every earlier command's punctuation had been kernel-
     generated, never typed). Verified in QEMU: `vfscat /system/...`
     and `vfscat /devices/...` back to back - the identical function
     call reaching two completely different mechanisms depending only
     on the path prefix - plus a `vfswrite` round trip confirming writes
     route too, and land in the exact same MiniFS directory a plain `ls`
     (with no idea VFS was involved) could still see.~~
7. ~~**Real `process.spawn()` from disk** - `proc/process.mc`'s
   `spawn_process_from_path()` calls `vfs_read()` into a scratch buffer, then
   hands the range to milestone 13's `spawn_process()` completely
   unchanged - loading from disk turned out to need no second loader,
   just a way to get the bytes into RAM first. New `install`/`spawn`
   shell commands: `install` writes the kernel's own compiled-in test
   program to a real MiniFS file via `vfs_write()`; `spawn` reads it back
   and launches an independent second process from it. Running a second
   ring3-capable process for the first time immediately reproduced the
   `TSS.RSP0` collision flagged as a prerequisite since milestone 13's
   own postmortem - a real GPF, not a hypothetical. Fixed for real this
   time: every task's own already-`kalloc`'d kernel stack (otherwise
   abandoned the moment it enters ring3 for good) doubles as its private
   RSP0 target, switched per-task in `yield()` alongside `cr3`. Verified
   in QEMU: the spawned process's own handle-query syscall reads back a
   task index distinct from the original process's, `ps` (now listing
   every process, not just the first) shows both with different `cr3`
   values, and the system stayed stable across extended repeated
   spawn/inspect cycles specifically hunting for any recurrence of the
   collision this fix was meant to close.~~
8. ~~**Method-call syntax in MiniC itself** (milestone 20, a compiler
   milestone in the `minic` repo, not this one) - `<ReturnType>
   <StructName>.<method_name>(<StructName>* self, ...)` declarations,
   mangled internally as `"StructName.method_name"` functions. Needed zero
   parser changes at call sites: `p.move(...)` already parsed as a struct
   field access being called, so the new logic lives entirely in
   codegen - recognize when the accessed field name matches a declared
   method and dispatch there, falling back to the pre-existing
   "function-pointer struct field" path unchanged when it doesn't. Pure
   syntax sugar, not a new dispatch mechanism - no vtables, no
   inheritance, no operator overloading; this kernel's own tag+if/else
   "polymorphic" routing (`proc/object.mc`, `disk/vfs.mc`) is untouched
   by it. See `minic`'s `examples/methods_demo.mc`.~~
   ~~**Milestone 21: a real compiled-MiniC program running in ring3** -
   every ring3 program until now was hand-assembled (`proc/testprog.s`);
   `spawn_process()`'s loader is a dead-simple "copy one contiguous byte
   range, jump to its first byte" model, which doesn't survive unmodified
   for anything `minicc` compiles, since string literals land in
   `.rodata` and globals in `.bss`/`.data` - separate ELF sections that
   `ld` groups by type across every input object, breaking contiguity
   the moment the compiled object links straight into `kernel.elf`
   alongside everything else. Fixed with a build-step change, not a
   compiler change: `proc/ring3prog.mc` (the new, real MiniC replacement
   for testprog.s) gets its own **separate** standalone link
   (`proc/ring3.ld`, `.text`/`.rodata`/`.data`/`.bss` placed contiguously
   with nothing else's sections in between) and gets flattened with
   `objcopy -O binary` before being wrapped in the same
   `g_test_prog_start`/`g_test_prog_end` markers everything downstream already
   used - zero changes needed in `proc/process.mc`, `kmain.mc`, or
   `shell.mc`. `asm(...)` has no operand binding (nothing lives in a
   register across a statement boundary in this codegen), so the new
   `do_syscall(num, arg1, arg2)` helper stages arguments into globals
   first, same pattern every other `asm()` block in this kernel already
   uses. Verified byte-for-byte identical behavior to the old
   hand-assembled version at every call site that used it: the boot-time
   automatic spawn, and the milestone-19 `install`+`spawn` disk path
   (which now installs a 0x250-byte compiled blob instead of the old
   0xd0-byte hand-assembled one, but produces identical message text and
   a correctly distinct `task_index` for the second process). Full
   regression pass (`tasks`/`procs`/`ps`/`objs`/`chan`+`send`/`disk`/
   `diskwrite`/`mkfs`/`mkfile`/`cat`/`ls`/`vfscat`/`vfswrite`/`alloc`/
   `map`) stayed clean.~~
   ~~**Milestone 22: a real native "File" API with real methods** -
   the first slice of the native System API, scoped to just `file`
   (`process`/`channel` wrappers are a later milestone - the point of
   this one was proving the whole pattern: new syscall + struct + real
   method, not building every resource type at once). Two new syscalls
   (`syscall/syscall.mc` numbers 4/5) expose the existing
   `disk/vfs.mc`'s `vfs_read`/`vfs_write` to ring3 for the first time -
   syscalls 1 and 3 only ever did print/handle-query before this. Since
   a syscall runs with the caller's own CR3 still loaded, dereferencing
   a ring3-supplied path/buffer pointer needed no new mechanism, the
   same as syscall 1's message pointer always has been. `do_syscall()`
   (`proc/ring3prog.mc`, from milestone 21) grew a third argument
   (`rdx`) to carry a buffer length alongside the existing path/buffer
   pointer pair. The `file` struct itself is deliberately minimal - just
   a `path`, since the underlying `vfs_read`/`vfs_write` are already
   whole-file, stateless operations with no open/close/seek - so
   `File.write(self, buf, len)`/`File.read(self, buf, max_len)` are real
   methods (milestone 20's syntax) wrapping `do_syscall()` internally,
   the first time this kernel's method-call syntax is used for
   something real rather than a language demo. Verified in QEMU: a
   fresh boot's `msg_file.write(...)` reports the exact real byte count
   of the message literal (`0x36`/54), `msg_file.read(...)` reads back
   the identical count and content, and a *second* boot reusing the
   same `disk.img` gets `0xffffffffffffffff` from `write()` (MiniFS's
   existing "fails if the name already exists" rule correctly firing
   against the first boot's file) while `read()` still succeeds with
   the same content - independent proof the first write reached real,
   persisted storage, not just something the ring3 process believed
   happened in memory.~~
   ~~**Milestone 23: real `process`/`channel` methods** - wrapping the
   other two the same way `file` was wrapped in milestone 22. Three new
   syscalls (numbers 6/7/8: spawn/channel_send/channel_receive) -
   `channel_receive` is this kernel's first ever *blocking* syscall,
   reusing `channel_receive()`'s existing `yield()`/`switch_context()`
   mechanism unchanged, since `syscall_dispatch` runs as an ordinary
   nested call within the calling ring3 task's own context (blocking
   there suspends the right task and resumes correctly through
   `isr_syscall`'s `iretq`, by the same mechanism already proven for a
   plain kernel-task caller). `spawn` reuses `spawn_process_from_path()`
   completely unchanged - the exact function the shell's own `spawn`
   command already called, just reached from ring3 for the first time.
   `channel.receive()`/`process.spawn()` (real methods, milestone 20
   syntax) let `proc/ring3prog.mc`'s own boot-time process block on an
   operator-triggered channel (`ring3go`, a new shell command) and then
   spawn a *second instance of itself* once triggered - deliberately
   sequenced this way (block-then-spawn, not spawn-at-boot
   unconditionally) specifically to avoid infinite self-replication: the
   spawned child reaches its own `channel.receive()` too, but the
   single-slot mailbox is already empty again (consumed by the parent),
   so it just blocks there forever instead of reaching its own `spawn()`
   call - no recursion-guard flag needed, the mailbox being single-slot
   already provides one for free. **A real bug found and fixed during
   this milestone**: the two channels (`g_channel_demo`, milestone 15;
   `g_ring3_channel_demo`, this milestone) initially got created in the
   wrong order relative to each other - `create_channel()` just returns
   the current count at call time, so which *global variable* a result
   gets assigned to has nothing to do with which index it receives, and
   the two milestones' hardcoded index assumptions (0 and 1) silently
   swapped. Symptom: the ring3 process's `channel.receive()` never woke
   up after `ring3go` (blocked forever, `ps` never showing a second
   process) - not a crash, just silent non-progress, since `g_channels[4]`
   has no bounds/identity checking to catch a request landing on the
   *wrong but still valid* index. Fixed by reordering the two
   `create_channel()` calls to match each hardcoded assumption instead of
   changing either constant. Verified in QEMU: `Channel.receive() got
   trigger 0x1` and `Process.spawn() launched task_index 0x9` print in the
   right order after `mkfs`/`install`/`ring3go`, the spawned child prints
   its own distinct `task_index` and repeats the whole demo sequence
   (including its own `file.write()`/`.read()` round trip) correctly in
   its own isolated address space, `ps` shows two real processes with
   different `cr3` values, and the shell stayed fully responsive
   (`chan`, `tasks`, `procs`, `objs`, `alloc`, `map` all clean) while the
   boot-time process sat genuinely blocked for an arbitrary number of
   other commands beforehand.~~
   ~~**Milestone 24: a thin POSIX-shaped shim** - `open`/`read`/`write`/
   `close`, plain free functions (deliberately not methods - matching
   POSIX's real API shape rather than extending the native OO-style
   one), implemented ENTIRELY in `proc/ring3prog.mc` with **no new
   syscalls and no kernel changes at all**. The native File API is
   whole-file only (no seek/position); a real fd needs to serve partial
   reads/writes and track a cursor across calls, so `open()` loads (or
   starts) a per-fd in-memory buffer once, `read()`/`write()` serve out
   of it while advancing a position, `close()` flushes a written buffer
   through `file.write()` in one shot - genuinely thin, a client-side
   library over the existing native API, not a new kernel mechanism.
   **This milestone also grew the compiled ring3 image past 4096 bytes
   (one page) for the first time, which surfaced three real, previously-
   latent bugs no earlier milestone's smaller program ever triggered**:
   (1) `objcopy -O binary` silently drops a *trailing* NOBITS (`.bss`)
   section instead of zero-padding it - every earlier milestone's tiny
   `.bss` usage was always write-before-read, so the missing zeros never
   actually mattered; milestone 24's `open()` is the first code here
   that reads a global (`g_fd_table[i].used`) before ever writing it,
   which would have read real garbage instead of a clean `false`. Fixed
   with `objcopy --set-section-flags .bss=alloc,load,contents`, forcing
   real zero bytes into the flattened blob. (2) Far more serious:
   `kmain.mc`/`shell.mc`/`ring3prog.mc`'s own `process.spawn()` call all
   hardcoded `load_vaddr=0x80000000`/`stack_vaddr=0x80001000` - safe only
   because every ring3 program until now fit in exactly one page. Once
   this one needed two, the image's own second page and the user stack
   landed on the *identical* virtual address, and whichever
   `map_page_in()` call ran second silently won, leaving the other
   completely unreachable there - manifesting as string literals
   resolving to the *correct address* (computed at compile time,
   independent of what's actually mapped there) but *wrong content*
   (whatever the stack overwrote), a genuinely confusing signature that
   cost real debugging time before being correctly diagnosed. Fixed by
   moving `stack_vaddr` out to `0x80020000` (128KB of headroom) at all
   three call sites. (3) `proc/process.mc`'s `g_loaded_image_buf` was a
   fixed 4096 bytes - exactly the limit its own comment had flagged
   since milestone 19 ("a real limit worth revisiting once anything
   bigger needs loading") - so `spawn_process_from_path()` started
   silently failing (MiniFS's own "file too large for the caller's
   buffer" sentinel) the moment the compiled program crossed that line.
   Bumped to 16KB. **A real debugging lesson from this milestone, not
   just the bugs themselves**: the string-literal-address-but-wrong-
   content symptom was initially very hard to separate from a genuinely
   *stale test artifact* - `rm -f serial.log` followed by a backgrounded
   QEMU launch that silently failed to (re)start left a 10-minute-old
   log file being misread as fresh output several times in a row before
   the mismatch was caught by checking file timestamps directly; a
   fully foreground `timeout N qemu-system-x86_64 ...` (no backgrounding
   at all) turned out to be the one invocation shape that reliably
   produces a genuinely fresh run in this environment. Verified in QEMU
   after all three fixes: the full boot sequence (File API, POSIX shim,
   Channel-gated Process.spawn, spawned child running its own complete
   demo) all produce correct output end to end, `ps` shows two real
   processes, and a full regression pass (`tasks`/`objs`/`alloc`/`map`)
   stayed clean.~~ Phase VIII (MiniC methods + native API + POSIX shim,
   milestones 20-24) is now complete.
9. **Capability/permission system** on top of the handle table, then
   security hardening (NX/ASLR/sandboxing).
   ~~**Milestone 25: real per-handle rights on `channel`** - Phase IX's
   first step. A new `Handle.rights` bitmask (`proc/object.mc`), fixed
   forever at grant time, and a new `OBJ_CHANNEL` object type wrap
   `channel` in the handle table for the first time - `channel_send`/
   `channel_receive` (syscalls 7/8) now take a real, rights-checked
   handle instead of a bare channel index, and a new `open_channel`
   syscall (number 9) is the one place a ring3 process can turn an index
   into a handle. Deliberately narrow policy: `open_channel` only ever
   grants `RIGHT_RECEIVE`, never `RIGHT_SEND` - nothing in this kernel
   today needs a ring3-initiated send (the shell/kernel side always
   sends directly), so this is a real, meaningful restriction, not a
   contrived one. `channel.open()` (real method, milestone 20 syntax) is
   new in `proc/ring3prog.mc`; `.send()`/`.receive()` unchanged in shape,
   just now handle-based underneath. Verified in QEMU: a fresh boot's
   `channel.open()` succeeds, an immediate `channel.send()` attempt
   through that exact handle is correctly REJECTED (`succeeded=0x0`) -
   the actual proof rights are enforced, not just present - while
   `channel.receive()` through the same handle still works normally
   afterward (blocks until `ring3go`, then unblocks with the right
   value). The independently spawned child process repeats the whole
   sequence correctly through its OWN separate handle table, and a full
   shell regression pass (`chan`/`send`/`procs`/`alloc`/`map`/`mkfile`/
   `vfscat`) stayed clean.~~
   ~~**Milestone 26: security hardening on the shared PDPT region** -
   Phase IX's second step. Tracing every actual `map_page`/`map_page_in`
   call site first showed the real, confirmed-vulnerable surface was
   narrower than the milestone-12-era note implied - the heap and the
   `map` demo already passed leaf flags without the user bit, so the gap
   was specifically `boot.s`'s static 1GB identity map's 2MB huge-page
   leaves (PDPT index 0). `mm/paging.mc`'s `clone_address_space()` now
   strips the user (0x04) bit from `new_pdpt[0]`/`new_pdpt[1]` when
   copying them from the kernel's own PDPT into a freshly cloned
   address space - `boot.s` and the original kernel-only `g_pml4_phys`
   space (never run in ring3) are untouched, only the cloned copy is.
   Verified with a genuine negative-space proof: a new shell command,
   `ring3fault`, sends a second trigger value on the existing ring3-demo
   channel, causing the boot-time process to deliberately attempt a
   forbidden write to `0x100000` - the kernel's own multiboot load
   address, definitely present, definitely never mapped by that process
   itself. The existing page-fault handler correctly reports
   `page fault at 0x100000, halting`, and the "forbidden write succeeded
   (BUG!)" line written specifically to catch a silent failure never
   prints. A full regression pass (heap, `map`, the scheduler,
   `mkfs`/`install`/`ring3go`'s spawn and isolation, milestone 25's
   capability rights) confirmed nothing legitimate broke.~~
   ~~**Milestone 27: real per-handle rights on Process** - Phase IX's
   third step, closing the gap milestone 25's own account left explicitly
   open. The real, concrete gap turned out to be narrower and more
   interesting than "Process has no rights at all": `RIGHT_QUERY` already
   existed (`proc/object.mc`, since milestone 25) and was already granted
   to every process's own self-handle (`spawn_process()`, since milestone
   14) - but syscall 3 (query) never actually checked it, resolving any
   valid `OBJ_PROCESS` handle regardless of its rights bitmask. Fixed by
   inlining the same handle-table/rights-check style syscalls 7/8/9
   already use (replacing the now-dead `resolve_handle()`, removed). New
   syscall 10 (`open_process`) is the first real CROSS-process capability:
   given another task's index, it mints a handle to that process in the
   caller's own table, granting the intersection of a caller-REQUESTED
   rights bitmask and what's actually grantable (`requested &
   RIGHT_QUERY` today) - a real, caller-controllable mechanism for the
   negative-space proof, not a testing-only backdoor. Verified in QEMU:
   after `process.spawn()` launches a child, the parent opens a handle to
   that CHILD with rights=0 (`ProcessHandle.open(rights=0) ok=0x1` - a
   real, valid handle that can do nothing) and its `.query()` is
   correctly rejected (`0xffffffffffffffff`); a second handle requesting
   `RIGHT_QUERY` succeeds and its `.query()` returns `0x9` - the exact
   task_index `process.spawn()` already reported, an independent
   cross-check. The pre-existing self-handle query (already granted
   `RIGHT_QUERY` since milestone 14, now actually verified for the first
   time) still returns the correct value too, confirming the newly-real
   check doesn't break the one path that's used it all along. A full
   regression pass (heap, `map`, the scheduler, milestone 26's
   `ring3fault` hardening, milestone 25's Channel rights) confirmed
   nothing broke.~~
   ~~**Milestone 28: NX enforcement on dynamically-mapped data regions**
   - Phase IX's fourth step, and the "security hardening (NX/ASLR/
   sandboxing)" item scoped down from three genuinely different
   mechanisms to just the first: NX/DEP, the classic W^X (write-xor-
   execute) protection real OSes use to stop injected shellcode from
   being jumped to and run. `boot.s` now sets EFER.NXE (bit 11) in the
   same rdmsr/wrmsr round trip as the existing EFER.LME - order matters,
   since setting a page table entry's NX bit (PTE bit 63) with NXE still
   off is a *reserved-bit* violation on every access to that page, not an
   execute-only restriction. `mm/paging.mc` gained `PAGE_NX` and widened
   `map_page_in()`'s flags mask to let it through to the leaf entry - and
   only the leaf, since the CPU **ORs** the NX bit across every
   translation level (the opposite of the "user" bit's AND semantics
   milestone 26 had to work around at an intermediate PDPT entry), so no
   intermediate-table change was needed this time. Applied to every
   dynamically-mapped region that's genuinely pure data: the heap
   (`mm/heap.mc`), the `map` shell demo, `proc_a`/`proc_b`'s private demo
   mapping (`sched/task.mc`), and - the real point - every ring3
   process's own user stack (`proc/process.mc`). Deliberately NOT applied
   to a loaded ring3 program's own code+data image: the loader still
   flattens a whole compiled program into one contiguous blob with no
   tracked code/data boundary (milestone 21's design), so a real W^X
   split *within* a loaded image remains a separate, larger, not-yet-
   tackled problem - noted explicitly, not silently left out.
   `isr/isr.mc`'s page-fault handler now also prints the raw error code -
   bit 4 distinguishes an instruction-fetch violation from a read/write
   one, letting this milestone's proof and milestone 26's `ring3fault`
   proof be told apart by their error codes alone (`0x15` vs `0x7`), not
   just by which shell command ran. New shell command `ring3nx` (same
   one-shot, kernel-halting shape as `ring3fault`) sends a third trigger
   value, causing the boot-time ring3 process to write a real `ret`
   opcode onto its own user stack and attempt to `call` it via a raw
   `asm(...)` block - if NX weren't enforced, this is completely benign
   (the `ret` just pops the pushed return address and jumps straight
   back), so the test is a real proof of a real mechanism, not a
   destructive one. Verified in QEMU: `page fault at 0x80020000,
   error_code=0x15, halting` - `0x15` (present + user + instruction-fetch)
   is the CPU's own hardware signal that this specific access was an
   execute attempt, not a coincidental fault. **A real bug found and
   fixed during this milestone's own regression pass**: `translate_in()`'s
   physical-address extraction only ever cleared the low 12 bits of a PTE
   before this, so a `PAGE_NX`-marked leaf's reported physical address
   came back with bit 63 still stuck in it - caught immediately by
   `procs`' own existing regression check (`@phys 0x8000000000440000`
   instead of `0x440000`) the moment `sched/task.mc`'s demo mapping
   started getting mapped with `PAGE_NX`, fixed by widening that
   function's own extraction mask the same way `map_page_in()`'s was
   already widened. A full regression pass after the fix (heap, `map`,
   the scheduler, `procs`' physical-address check specifically re-verified
   correct, `mkfs`/`install`/`ring3go`'s spawn+isolation+milestone-27
   rights, `ring3fault` re-verified with its own now-distinguishable
   error code) confirmed everything else stayed clean.~~ **Phase IX is
   now considered substantially complete** - ASLR and true process
   sandboxing remain open, unscoped ideas for a future phase revisit
   (see Known limitations), but the concrete gaps this phase set out to
   close (capability rights on both `channel` and `process`, and real
   memory-protection hardening on the shared kernel region, including
   NX) are all done. Next up: Phase X (driver framework + networking).
10. **A real driver framework (PCI enumeration) + networking** (NIC
    driver, a from-scratch TCP/IP stack) - deliberately last: the
    largest remaining subsystem, with the fewest things depending on it.
    ~~**Milestone 29: PCI bus enumeration** - Phase X's first step, and
    the necessary prerequisite for everything else in this phase: a NIC
    driver can't be written against a hardcoded, assumed I/O port the
    way the ATA driver's 0x1F0 was - it has to be *discovered*. New
    `drivers/pci.mc` walks the legacy CONFIG_ADDRESS/CONFIG_DATA
    mechanism (ports 0xCF8/0xCFC, `drivers/io.mc`'s new `outl`/`inl`
    32-bit port I/O) across bus 0's 32 device slots, checking function 0
    of each and recursing into functions 1-7 only for devices whose
    header-type byte marks them multi-function. Deliberately scoped to
    bus 0 only, no PCI-to-PCI bridge recursion - QEMU's default machine
    has none, and walking one is a real but separate extension for
    whenever a device beyond bus 0 actually needs finding. Verified with
    a genuinely strong claim for this kind of low-level driver: every
    device found (`0x8086:0x1237` host bridge, `0x8086:0x7000`/`0x7010`/
    `0x7113` ISA/IDE/ACPI bridges, `0x1234:0x1111` Bochs VGA,
    `0x8086:0x100e` an Intel e1000 NIC) is a real, publicly-documented
    vendor:device ID pair matching QEMU's own default machine model
    exactly - not something this driver could fake its way into looking
    right, and `0x7010` is literally the same IDE controller the
    existing ATA driver already talks to, now independently discovered
    rather than assumed. Finding a real e1000 NIC with zero extra QEMU
    flags is a useful, unplanned bonus: the next Phase X milestone (a
    NIC driver) has a real, already-present target device to write
    against. A regression pass (`disk`/`diskwrite` interleaved with
    `pci`, confirming the new 32-bit port-I/O globals don't corrupt the
    ATA driver's own shared port-I/O staging globals; heap; scheduler)
    stayed clean.~~
    ~~**Milestone 30: a real e1000 NIC driver (initialization only)** -
    Phase X's second step, deliberately scoped to just enabling the
    device and reading back real hardware state, NOT sending or
    receiving an actual packet yet - setting up the RX/TX descriptor
    rings is a separably-sized hard problem of its own, the same
    "narrowest safe first version" discipline milestone 29 used for bus
    0 only. New `net/e1000.mc`: enables the device over PCI (Memory
    Space + Bus Master, `drivers/pci.mc`'s new `pci_config_write_dword()` -
    this kernel's first PCI config space WRITE, every earlier use was
    read-only), reads BAR0 (`pci_read_bar0()`) to find its real physical
    MMIO base, and maps 8 pages of it into the kernel's own address
    space at a fixed convention (`0x60000000`) via `mm/paging.mc`'s
    ordinary `map_page()` - the first time this kernel has mapped device
    registers rather than RAM, though the mapping mechanism itself
    doesn't care which. A real, load-bearing assumption verified
    correct rather than just hoped: BAR0 actually holds a valid,
    already-assigned physical address, because QEMU's `-kernel` boot
    still runs SeaBIOS first (multiboot loading is one of SeaBIOS's own
    jobs, not a BIOS bypass) - real PCI bus enumeration and BAR
    assignment already happened before this kernel ever got control,
    confirmed empirically rather than assumed, since a genuinely
    unassigned BAR would have meant mapping physical address 0 or
    garbage. Reads the real MAC address straight from `RAL0`/`RAH0`
    (pre-loaded by QEMU's own emulated EEPROM at power-on, same as real
    hardware auto-loading its burned-in address) rather than
    implementing the separate EEPROM-read protocol just to re-derive a
    value already sitting in plain registers. Verified independently,
    the same "checkable against something the kernel had no part in
    choosing" property as milestone 29's own proof: the real MAC read
    back, `52:54:00:12:34:56`, is QEMU's own well-known, publicly
    documented default NIC MAC address - not a number this driver could
    fake its way into producing by accident - and `link_up=0x1` confirms
    a second, separate piece of real hardware state. A regression pass
    (`pci` re-run after the new PCI config WRITE, confirming it didn't
    disturb other devices' config space; `disk`/`diskwrite`/`mkfs`;
    heap; paging; scheduler) stayed clean.~~
    ~~**Milestone 31: real TX/RX descriptor rings and a genuine packet
    round trip** - Phase X's third step, building directly on milestone
    30's own groundwork (MMIO already mapped, PCI bus-mastering already
    enabled) rather than starting over. `net/e1000.mc` gained
    `packed struct TxDescriptor`/`RxDescriptor` matching the hardware's
    own 16-byte layout exactly, `e1000_init_rings()` (allocates and
    programs both rings - TDBAL/TDBAH/TDLEN/TDH/TDT and TCTL/TIPG for
    TX, the RX equivalents plus RCTL for RX, using `alloc_frame()`'s own
    frames directly as physical DMA buffer addresses, the same
    already-established convention `proc/process.mc`'s loader uses),
    `e1000_send()` (copies into a buffer, hands a descriptor to the
    hardware, polls its own DD status bit for real hardware confirmation
    - the same bounded-poll, fail-clean discipline the ATA driver's
    `ata_wait_ready`/`ata_wait_drq` already established), and `e1000_receive()`.
    Deliberately NOT a real ARP subsystem (no address resolution cache,
    no general request/reply handling) - the new `arp` shell command
    crafts exactly ONE hardcoded, valid Ethernet+ARP request asking "who
    has 10.0.2.2" (QEMU SLIRP's own well-known default gateway), purely
    as a real external stimulus to prove RX genuinely works, the same
    "one hardcoded test case, not a general framework" scope milestone
    26's `ring3fault` already used. **A real fix found during testing,
    not assumed correct from the start**: the first attempt's RX poll
    used a raw instruction-count spin bound, which returned `reply
    len=0x0` - not a driver bug, but a measurement one: a tight spin
    loop doesn't reliably correspond to any particular amount of real
    wall-clock time, and a genuine external SLIRP round trip needs real
    time to happen. Fixed by polling against `isr/isr.mc`'s own
    `g_tick_count` instead - the same lesson this project's own QEMU/TCG
    timer-rate gotcha already taught, applied to a new context. Verified
    with the strongest proof this driver has produced yet: TX confirmed
    by the hardware's own descriptor-done bit, and RX confirmed by a
    genuine reply from QEMU's real external network stack -
    `from=52:55:0a:00:02:02`/`sender_ip=10.0.2.2` are QEMU's own
    documented default gateway MAC/IP, not anything this kernel could
    have fabricated. A full regression pass (heap, paging, scheduler,
    `nic`, `pci`, `disk`/`diskwrite`) stayed clean.~~
    ~~**Milestone 32: a real ARP resolver** - Phase X's fourth step,
    replacing milestone 31's own hardcoded one-shot test packet with a
    genuine client-side resolver, built on the proven TX/RX plumbing
    rather than starting over. New `net/arp.mc`: a fixed-size cache
    (`arp_entry[8]`), `arp_resolve(ip, mac_out)` - checks the cache first,
    and only on a miss sends a real request and polls for a genuinely
    matching reply (right EtherType, right opcode, right sender IP,
    ignoring anything else that might arrive) against a real
    `g_tick_count`-bounded timeout. Deliberately scoped as a CLIENT
    (resolver) only, not a responder - this kernel has no assigned IP
    address of its own in any real sense yet, so answering "who has
    &lt;our address&gt;" doesn't mean anything until it does. The shell's
    `arp` command (same name as milestone 31's, now backed by the real
    mechanism) runs four checks in one pass: resolve the gateway (a real
    external round trip, `elapsed_ticks=0x64` - genuine, non-trivial
    wall-clock time), resolve it AGAIN (`cached_elapsed_ticks=0x0` - a
    real, timing-based proof of the cache, not just "same answer
    twice"), resolve a SECOND, different real address (QEMU SLIRP's own
    DNS proxy, `10.0.2.3` - proves the resolver genuinely generalizes,
    not something special-cased for one fixed IP), and resolve an
    address nothing answers for (`10.0.2.99` - correctly returns false
    after the real timeout, not a hang). A full regression pass (heap,
    paging, scheduler, `pci`, `nic`, `disk`/`diskwrite`) stayed clean.~~
    ~~**Milestone 33: a real IPv4 layer, verified with a genuine ICMP
    echo (ping) round trip** - Phase X's fifth step, and the first
    milestone to give this kernel a real, explicit concept of "my IP
    address" (`net/ip.mc`'s `g_my_ip`, previously a bare literal
    duplicated inside `net/arp.mc`'s own request-building code, now one
    shared, named value both files use). New `net/ip.mc`: `ip_checksum()`
    (the real RFC 791 one's-complement checksum, also reused for ICMP's
    identical algorithm) and `ip_build_header()` (a real 20-byte, no-options
    IPv4 header). New `net/icmp.mc`: `icmp_ping()` - the minimal, natural
    verification vehicle for "does IP genuinely work end to end," the
    same relationship milestone 31's hardcoded ARP request had to proving
    TX/RX worked at all (a bare IP packet with no upper-layer protocol
    has nothing that could reply to it). Resolves the target's MAC via
    milestone 32's real `arp_resolve()` first, builds a real
    Ethernet+IPv4+ICMP echo request, sends it, and polls (real
    `g_tick_count`-bounded, milestone 31/32's already-learned timing
    lesson) for a reply matching on EVERY real field: EtherType, IP
    protocol, source IP, ICMP type, AND the exact identifier/sequence
    number sent - not just "something ICMP arrived." Verified in QEMU:
    `ping gateway ok=0x1 elapsed_ticks=0x64` - a genuine reply from QEMU's
    real network stack, the same non-trivial round-trip time `arp`
    already measured, no bugs found (the hard, novel risk - DMA rings,
    MMIO, the tick-vs-spin timing lesson, real external verification -
    was already absorbed by milestones 31/32; this one was a clean
    application of already-proven mechanisms to a new protocol layer). A
    full regression pass (heap, paging, scheduler, `pci`, `arp`'s own
    continued correctness after the `g_my_ip` refactor) stayed clean.~~
    ~~**Milestone 34: a real UDP layer, verified with a genuine DNS
    query** - Phase X's sixth step. UDP is the simpler of the two
    remaining transport protocols - no connection state, no
    retransmission/congestion control, just a port-addressed datagram
    wrapper over the now-working IP layer; TCP's real connection state
    machine remains a substantially bigger, separate hard problem, still
    ahead. New `net/udp.mc`: `udp_checksum()` (the real pseudo-header
    algorithm - source IP, dest IP, a zero byte, the protocol number,
    and the UDP length, THEN the real header+payload, reusing
    `ip.mc`'s `ip_checksum()` unchanged - binding the checksum to the
    addresses it's delivered between, not just its own bytes),
    `udp_build_header()`, and `udp_send()`/`udp_receive()` (the same
    resolve-then-send-then-tick-bounded-poll shape milestone 33's ICMP
    code already established). New `net/dns.mc`: `dns_query()` - a
    minimal, hand-crafted DNS query (deliberately NOT a real DNS client
    - no compression, no caching, no record types beyond A), purely as a
    real external verification vehicle for UDP, the same "one hardcoded
    real-protocol message as a test vehicle" discipline ARP's request
    and ICMP's ping both already used. Sent to QEMU SLIRP's own built-in
    DNS proxy (`10.0.2.3`) and verified on the exact transaction ID sent,
    the response bit, and a real answer count - proving SLIRP genuinely
    forwarded the query to a real upstream resolver. Verified in QEMU:
    `dns query ok=0x1 elapsed_ticks=0x74` - noticeably higher than
    `ping`'s own `elapsed_ticks=0x64`, a real, sensible detail (DNS
    resolution genuinely takes one more hop than ICMP echo, which SLIRP
    answers directly). No bugs found in the network protocol logic
    itself - the only issues hit were two ordinary MiniC type-mismatch
    compile errors (mixed `int`/`u16` arithmetic in an argument
    expression, and in an array index using a `u16` loop counter instead
    of the established `int` convention), fixed by restructuring rather
    than casting inline. A full regression pass (heap, paging, scheduler,
    `pci`, `ping`'s own continued correctness) stayed clean.~~

    ~~**The MiniC → C rewrite** - right as TCP was about to get its own
    dedicated scoping pass, the decision was made to retire MiniC (and
    its own compiler/docs repos) entirely and rewrite this whole kernel
    by hand into freestanding C first, for faster day-to-day development
    going forward. Every one of the ~34 `.mc` files (and `shell.mc`, kept
    around one stage longer than the rest purely as a porting reference)
    was rewritten and independently re-verified in QEMU against its own
    already-known-good milestone output before being deleted - not a
    bulk mechanical translation trusted on faith. `os/CLAUDE.md`'s
    architecture notes cover the toolchain mechanics (why the build still
    produces an ELF32 container for genuinely 64-bit code, the `-fPIC`/
    `-fvisibility=hidden` flags that pipeline needs that MiniC's own
    simpler codegen never did). One real, C-specific bug was found and
    fixed during the rewrite: `spawn_process()`'s "jump to byte 0 of the
    loaded image" loader assumed the entry function always lands first,
    true for MiniC's own single-pass codegen but not for gcc, which laid
    out `ring3prog.c`'s functions in a different order - fixed with a
    dedicated linker section pinning `_start` to offset 0 regardless of
    source order. Everything else carried over unchanged in behavior:
    same architecture, same milestones, same verification bar, same "no
    external libraries" rule - only the implementation language changed.
    See the "A note on language history" paragraph at the top of this
    file for the full framing, and the git history for one commit per
    rewrite stage.~~

    ~~**Milestone 35: a real TCP client, verified with a genuine HTTP GET
    round trip to a real internet host** - Phase X's seventh step, the
    first genuinely stateful transport protocol in this kernel and the
    first time any protocol layer here has talked to something off the
    local SLIRP subnet. New `net/tcp.c`: `tcp_checksum()` (the same
    pseudo-header shape UDP's own checksum uses, just protocol number 6
    and a variable-length segment instead of a fixed 8-byte header),
    `tcp_build_header()` (a real 20-byte header, no options), and
    `tcp_fetch()` - one client-only, single-connection, no-retransmission
    round trip: a real 3-way handshake (SYN, wait for a genuinely
    matching SYN-ACK - right ACK number, not just "something came back" -
    then ACK), a real data segment carrying an actual HTTP/1.1 GET
    request, a receive loop that ACKs each arriving segment and keeps
    polling until the peer's FIN or the caller's buffer fills, and a
    best-effort graceful close (FIN/ACK both directions - not required
    for the milestone's own success criterion, so a close timeout doesn't
    flip the overall result). Extended `net/dns.c` with a real
    `dns_resolve_a()` alongside the existing `dns_query()` - actually
    parses the answer section (handling both a DNS-compression name
    pointer and a literal one, and skipping past any non-A record like a
    CNAME rather than assuming the first answer is always the one
    wanted) instead of just checking the header, so the TCP demo's target
    is a real, freshly-resolved IP rather than a hardcoded address that
    could go stale.
    - **The real routing distinction this milestone had to get right,
    that no earlier protocol layer here ever needed to**: every previous
    ARP/ICMP/UDP target (the gateway, the DNS proxy) sat on the local
    10.0.2.0/24 SLIRP subnet, so ARPing the target IP directly and
    sending straight to its own MAC always worked. A real internet host
    doesn't answer ARP requests from this guest at all - `tcp_fetch()`
    ARP-resolves the GATEWAY's MAC for the Ethernet-level destination
    while putting the real remote IP in the IP header itself, the same
    "next hop vs. final destination" distinction every real router relies
    on. Getting this backwards (ARPing the real target IP) would time out
    silently with no useful error - worth remembering before any future
    protocol work assumes "resolve target, send to it" is always correct.
    - Verified in QEMU, twice, independently: `resolved example.com ->
    0xac.0x42.0x93.0xf3 tcp_fetch_ok=0x1 response_len=0x200
    elapsed_ticks=0x34 got_http_status=0x1` and, in a second separate
    boot, `resolved example.com -> 0x68.0x14.0x17.0x9a tcp_fetch_ok=0x1
    response_len=0x200 elapsed_ticks=0x4 got_http_status=0x1` - two
    *different* real IPs (Cloudflare's own round-robin DNS answering
    differently each time, both independently confirmed as genuine
    answers for example.com via a real `curl` from this same
    environment), both producing a complete handshake, a full
    512-byte real HTTP response (`response_len=0x200` - the receive
    buffer's own cap, meaning the real reply was at least that long, most
    likely more given `Transfer-Encoding: chunked`), and a confirmed real
    `HTTP` status line at the start of it - not "some bytes arrived," a
    specific, checkable claim about what those bytes actually are. `tasks`
    run immediately afterward in the same session confirmed the scheduler
    and every other kernel task stayed healthy through the exchange.
    - **Deliberately still open**: no retransmission or congestion
    control (a lost segment just times out, same as every prior protocol
    layer's "narrowest safe first version" scope), no listening/server
    side, one connection at a time with no connection table, no options
    (no window scaling, no SACK), close is best-effort only (a peer that
    never FINs after the milestone's own success criterion is already met
    just gets abandoned, not tracked as a real leak anywhere). These are
    real, substantial follow-on work, not oversights.~~ Next up: Phase IX
    (capability/permission work + security hardening).
11. **Service architecture + a real `init`** - the current hardcoded
    `shell/shell.c` loop migrates to an actual userspace program once
    processes/IPC/VFS exist to support that; async I/O as a cross-cutting
    pass once sync I/O works everywhere.

Self-hosting used to be framed as "`minicc` compiled for MiniC OS's own
target, running *on* MiniC OS to compile something" - that goal doesn't
carry over as-is now that MiniC itself is retired; getting a real C
toolchain running natively on this kernel is a substantially bigger
undertaking than porting one small hand-rolled compiler was, and isn't
a near-term goal. The real prerequisites this kernel already has either
way (native API, real file I/O, real process spawning) mean the door
isn't closed - just not something to plan around until much later.

## Known limitations (on purpose, for now)

- ~~The shared region every address space carries (PDPT[0]/PDPT[1] - the
  static identity map plus the heap) is still marked user-accessible~~ -
  **fixed in milestone 26**: `mm/paging.mc`'s `clone_address_space()` now
  strips the user (0x04) bit from `new_pdpt[0]`/`new_pdpt[1]` when copying
  them from the kernel's own PDPT into a freshly cloned (ring3-capable)
  address space, closing the gap this note flagged since milestone 12.
  Tracing every actual leaf-level `map_page`/`map_page_in` call site first
  showed the real, confirmed-vulnerable surface was narrower than this
  note implied - the heap (`mm/heap.mc`) and the `map` demo command
  already passed flags without the user bit at the leaf, so the gap was
  specifically `boot.s`'s static 1GB identity map's 2MB huge-page leaves
  (PDPT index 0), reachable only through the AND-across-levels PDPT-entry
  permission this fix closes. `boot.s` itself and the original kernel-
  only address space (`g_pml4_phys`, used directly by plain kernel tasks
  that never enter ring3) are untouched - the fix lives entirely in the
  *copy* a cloned space gets, since x86 paging ANDs the user bit across
  every level from PML4 down to the leaf, and every legitimate ring3
  interaction with kernel code/the heap already goes through a syscall
  (which raises CPL to 0 before touching that memory) rather than a
  direct ring3 access. Verified with a genuine negative-space proof, not
  just a topology change: a new shell command (`ring3fault`) sends a
  second trigger value on the existing ring3-demo channel, causing
  `proc/ring3prog.mc`'s boot-time process to deliberately attempt a
  forbidden write to `0x100000` (the kernel's own multiboot load
  address) - the existing page-fault handler correctly reports
  `page fault at 0x100000, halting` and the kernel halts right there, the
  "forbidden write succeeded (BUG!)" line specifically written to prove
  this a real reject (never printed) rather than a silent no-op. A full
  regression pass (heap, the `map` demo, the scheduler, `mkfs`/`install`/
  `ring3go`'s process spawn and isolation, milestone 25's capability
  rights) confirmed nothing legitimate broke.
- ~~`process` handles carried no real rights enforcement - `RIGHT_QUERY`
  existed and was granted, but nothing ever checked it~~ - **fixed in
  milestone 27**: syscall 3 (query) now checks `RIGHT_QUERY` before
  resolving a handle, the same inline handle-table/rights style syscalls
  7/8/9 already used (`resolve_handle()`, which had no rights concept,
  removed as dead code once every caller needed one). New syscall 10
  (`open_process`) mints a handle to ANY process (not just yourself) with
  a caller-requested rights bitmask intersected against what's actually
  grantable - verified by opening a rights-less handle to a freshly
  spawned child and watching its `.query()` get rejected, then opening a
  second, properly-rights handle and watching it succeed with the exact
  task_index `process.spawn()` already reported.
- ~~No NX (no-execute) enforcement anywhere - every writable mapping was
  also executable~~ - **fixed in milestone 28**: `boot.s` sets EFER.NXE
  at boot, `mm/paging.mc` gained `PAGE_NX` (PTE bit 63, OR'd across
  translation levels - only the leaf entry ever needs it, unlike the
  user bit), and every dynamically-mapped pure-data region (the heap,
  the `map` demo, `proc_a`/`proc_b`'s demo mapping, and - the real
  point - every ring3 process's own user stack) is marked non-
  executable. Verified with a real negative-space proof: the shell's
  new `ring3nx` command has the boot-time ring3 process write a `ret`
  opcode onto its own stack and attempt to execute it - the resulting
  page fault's error code (`0x15`) has the CPU's own instruction-fetch
  bit set, distinguishing it from an ordinary read/write fault
  (`ring3fault`'s own `0x7`). **Deliberately still open**: a loaded
  ring3 program's own code+data image is NOT split - the loader still
  flattens a whole compiled program (code, rodata, data, bss) into one
  contiguous blob with no tracked boundary between them (milestone
  21's design), so the WHOLE image stays executable, same as before.
  A real W^X split within a loaded image (tracking where `.text` ends
  so only that part stays executable) is a separate, larger problem -
  the loader would need to learn a real boundary, not just a start/end
  byte range - not tackled here. ASLR and real process sandboxing
  (beyond address-space isolation, which has existed since milestone
  12) are two more genuinely different mechanisms this same "security
  hardening" label used to bundle together - both remain completely
  unscoped, open ideas for a future phase, not silently forgotten.
- ~~Only one ring3 process was safe to run at a time~~ - **fixed in
  milestone 19**: `sched/task.mc`'s `task` gained a `kernel_stack_top`
  field (each task's own already-`kalloc`'d kernel stack, reused as its
  private `TSS.RSP0` target since it's otherwise abandoned the moment
  `run_ring3_test()` iretqs into ring3 for good), and `yield()` calls
  `mm/paging.mc`'s `set_tss_rsp0()` for the incoming task whenever it's
  ring3-capable, right alongside the existing `load_cr3()`. Verified with
  two concurrently-scheduled ring3 processes running stably through
  extended repeated testing. **A residual concern, not fully closed**:
  during milestone 19 development, one *unreproduced* crash (a page
  fault, not the RSP0 GPF signature) was observed once during repeated
  `procs` testing and never recurred across 25+ further attempts,
  including deliberately aggressive stress sequences designed to
  reproduce it. Most likely an environment-specific fluke (a QEMU/WSL
  timing hiccup) rather than a logic bug - the per-task RSP0 design was
  reasoned through carefully and matches the exact fix the milestone-13
  postmortem called for - but flagged here rather than silently assumed
  fixed, since it was never conclusively root-caused. Revisit if it ever
  recurs with a reproducible trigger.
- A separate, confirmed-harmless, pre-existing artifact (reproduces even
  on milestone 18's code, unrelated to the RSP0 work above): the *very
  first* `procs` check after boot has occasionally shown `proc_a`'s or
  `proc_b`'s `@phys` as a small, clearly-wrong-looking value (e.g.
  `0x20`) instead of a real frame address - every subsequent `procs`
  call in the same session shows the correct value from then on. Looks
  like a narrow timing window in the demo tasks' very first scheduling
  round, not a real correctness issue (the mechanism it's demonstrating,
  per-process isolation, has been independently verified solid many
  other ways) - not yet root-caused, noted here rather than ignored.
- `clone_address_space()`'s PML4/PDPT/PT frames (and a spawned process's
  image/stack frames, and now its `kernel_object`/handle-table entries)
  are never freed - there's no process teardown at all yet (nothing
  calls `free_frame` on any of it, no "close handle"/"free object"
  exists), consistent with the kernel having no process *exit*
  mechanism at all so far.
- `proc/object.mc`'s tables are fixed-size arbitrary constants (8
  `kernel_object`s total, 8 handles per process, matching `g_processes`'
  4-process cap) - fine for today's one loaded process, would need real
  sizing (or dynamic growth) once more than a handful of objects/
  processes exist at once.
- Only two kernel object types exist (`OBJ_PROCESS`, `OBJ_CHANNEL` as of
  milestone 25) - a `task` (the scheduler's own thread-of-control
  concept) isn't a kernel object in its own right yet, unlike real NT
  where Process and Thread are separate object types. Not a gap so much
  as not-yet-needed: nothing today creates more than one thread per
  process to distinguish. ~~A `channel` (milestone 15) isn't wrapped as
  a `kernel_object`/handle either yet - `channel_send`/`channel_receive`
  (and, as of milestone 23, the syscalls exposing them to ring3) take a
  plain integer `g_channels[]` index, bounds-checked at the syscall
  boundary but not otherwise access-controlled - any ring3 process that
  knows (or guesses) a valid index can send/receive on any channel, not
  just one it was actually given.~~ - **fixed in milestone 25**: a new
  `open_channel` syscall (number 9) wraps a channel index in a real
  `OBJ_CHANNEL` object and hands back a genuine handle, and syscalls 7/8
  now take that handle (not a raw index), checking real per-handle
  `rights` (a bitmask, fixed forever at grant time) before touching the
  underlying channel at all - `open_channel` deliberately only ever
  grants `RIGHT_RECEIVE`, so a handle obtained this way genuinely cannot
  be used to send, verified by a real rejected `channel.send()` attempt
  in `proc/ring3prog.mc`'s own boot-time demo, not just assumed correct.
  This is Phase IX's first real step (capability/permission work on top
  of the handle table) - the shared-PDPT memory-protection gap above and
  broader security hardening (NX/ASLR/sandboxing) are still open.
- `channel_send()` is non-blocking and single-slot: a sender that finds
  the mailbox already full just gets `false` back, with no queue, no
  backpressure, and no way to wait for room - fine for today's one
  sender/one receiver demo, not a real MPSC/SPSC queue. A blocking send
  (the sender waiting the same way `channel_receive()`'s caller does) is
  a separate hard problem, deliberately not tackled this milestone.
  `proc/channel.mc`'s `g_channels[4]` cap is an arbitrary constant, same
  reasoning as `proc/object.mc`'s tables above.
- Task/process/object/channel creation failures are silently ignored in
  `kmain.mc` - `create_task`/`create_isolated_task`/`spawn_process`/
  `create_channel` all return a success flag or index, but none of their
  boot-time call sites check it. Harmless today (none of them are
  anywhere near their fixed caps at boot), but a real bug class waiting
  to happen once boot-time setup grows enough to actually hit one - this
  is exactly how milestone 15's own `g_tasks[8]` capacity bug went
  unnoticed until a live QEMU test caught it, rather than a build error.
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
  arbitrary constants (`mm/heap.mc`'s `g_heap_cap` / `heap_grow`'s minimum
  chunk) picked for "plenty for a hobby kernel," not computed from actual
  available memory or tuned for any real workload.
- The heap only ever grows, never shrinks - `kfree` can leave large
  trailing free regions, but nothing unmaps pages and returns frames to
  the allocator once they're no longer needed.
- `map_page` must not be called on a virtual address that falls inside
  boot.s's static 1GB identity map (PDPT index 0, i.e. any address below
  1GB) - the PD entries there are 2MB huge pages (PS bit set), and
  walking past one as if it pointed to a PT would read a garbage table
  address. Every caller today (the `map` shell command) stays above 1GB
  specifically to avoid this; nothing enforces it automatically yet.
- No unmap / page-table teardown - `map_page` only ever adds entries and
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
- `sleep()` blocks by tick count, and milestone 15's `channel_receive()`
  added a real wait-on-message primitive - but there's still no
  semaphore/mutex, no wait-on-I/O-completion, and no way for one task to
  wake another early except by sending it a message. `yield()`'s scan
  for a runnable task is O(n) in the task count every call, fine for the
  handful of tasks here.
- The scheduler has a fixed-size task table (`sched/task.mc`'s
  `g_tasks[16]` as of milestone 15) and each task's 16KB stack is
  `kalloc`'d once and never freed - fine for a handful of long-lived
  demo tasks, not a real process model.
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
- `disk/ata.mc` talks only to the primary bus's master drive (port base
  `0x1F0`, drive-select byte hardcoded to `0xE0 | ...`) - no secondary
  bus, no slave drive, no drive detection/identification (`IDENTIFY`
  command) to confirm a drive is even there before trying to use it. No
  AHCI/NVMe support. ~~No PCI enumeration to find controllers
  dynamically~~ - **PCI enumeration itself now exists** (milestone 29,
  `drivers/pci.mc`, bus 0 only - see the roadmap), and it correctly
  finds the same IDE controller (`0x8086:0x7010`) this driver already
  talks to by hardcoded port - but `disk/ata.mc` hasn't been retrofitted
  to actually USE that discovery instead of its own fixed `0x1F0`. Two
  separate gaps, only the first one closed so far.
- `drivers/pci.mc`'s enumeration is bus 0 only - no recursion through a
  PCI-to-PCI bridge's secondary bus (QEMU's default machine has none, so
  nothing has needed this yet), and there's no general "driver
  registration framework" beyond the one enumeration function itself -
  finding a device by vendor:device ID today means a caller manually
  scanning `g_pci_devices[]`, not a real driver-matching/probe mechanism.
- ~~No NIC driver exists yet either~~ - **`net/e1000.mc` now sends and
  receives real packets** (milestones 30-31): the e1000 is enabled over
  PCI, its MMIO register file is mapped, real hardware state reads back
  correctly, and real TX/RX descriptor rings move genuine traffic -
  verified with an actual external round trip through QEMU's own
  network backend. ~~No real ARP subsystem~~ - **`net/arp.mc` now exists**
  (milestone 32): a genuine cache, `arp_resolve()` working for any target
  address, a real tick-bounded timeout on a miss. ~~No IP layer~~ - **a
  real one exists now** (milestone 33, `net/ip.mc`/`net/icmp.mc`): real
  IPv4 header construction, a real RFC 791 checksum, and a genuine ICMP
  echo (ping) round trip. ~~No UDP~~ - **a real one exists now**
  (milestone 34, `net/udp.mc`): real pseudo-header checksums,
  `udp_send()`/`udp_receive()`, verified with a genuine DNS query to
  QEMU's built-in resolver proxy. **Still fully open**: ARP remains
  client (resolver) only - this kernel never answers "who has &lt;our
  address&gt;." This kernel's own IP address (`net/ip.mc`'s `g_my_ip`,
  milestone 33) is a fixed, static assumption, not real DHCP-negotiated
  configuration - `10.0.2.15`, QEMU SLIRP's conventional default guest
  address, not something this kernel actually negotiated or owns. The
  ARP cache has no eviction/TTL - entries, once resolved, are trusted
  forever within one boot. No TCP - a substantially bigger, genuinely
  separate hard problem (real connection state machine, sequence
  numbers, retransmission), still fully ahead; ICMP is the only
  IP-layer protocol implemented, and only its echo request/reply
  message type at that (no destination-unreachable, no time-exceeded,
  no other ICMP messages). `net/dns.mc`'s `dns_query()` is deliberately
  NOT a real DNS client - no response caching, no compression-pointer
  decoding, no record types beyond A, no reuse of the transaction
  outside this one demo call - it exists purely as an external test
  vehicle for UDP, the same role ARP's request and ICMP's ping played
  for their own layers. `net/e1000.mc`'s register offsets and init
  sequence are
  e1000-specific - no generic NIC abstraction exists (nor would one make
  sense yet, with only one NIC driver in existence). The RX/TX rings are
  fixed-size (8 descriptors each) with no dynamic growth, matching every
  other fixed-size table in this kernel; `e1000_send()`/`e1000_receive()`
  both poll rather than using the device's own interrupt capability, the
  same "poll first, interrupts later" precedent the ATA driver already
  established.
- The ATA driver is polling-only (busy-waits on the status register), not
  interrupt-driven - simpler to get right first, same reasoning PIO came
  before AHCI/NVMe, but it means a disk operation blocks whichever task
  issued it (and, since `int 0x80`/hardware interrupts aren't disabled
  during the wait, everything else keeps preempting normally around it -
  just not the caller itself) rather than yielding the CPU while waiting.
- `disk.img` (via `build.sh disk`) is a fixed-size (1MB/2048-sector), raw
  image - milestone 16's test signature at LBA 1, `diskwrite`'s scratch
  sector at LBA 100, and milestone 17's MiniFS region from LBA 500
  onward all coexist by convention (chosen-to-not-overlap LBA ranges),
  not a real partition table. A real partition scheme is later work,
  once more than one filesystem/reserved-region needs to coexist for a
  reason other than "this kernel's own test history."
- `disk/minifs.mc` is deliberately minimal in every dimension: flat
  namespace (no directories/subdirectories), a fixed 16-file directory
  cap, no file deletion or truncation (`fs_write_file` fails outright if
  the name already exists - no overwrite/append/resize), no persistent
  free-space tracking (`fs_write_file` rescans the whole directory to find
  the next free LBA every call - O(files) per write, fine at this
  scale), and no error recovery if a write is interrupted partway
  (no journaling, no atomicity - a crash mid-write leaves the directory
  and data region in whatever state the individual sector writes that
  did complete left them in). All deliberate scope cuts for a first
  filesystem, not oversights - see milestone 17's roadmap entry for the
  reasoning.
- `disk/vfs.mc`'s mount table is fixed-size (4 entries) and flat - no
  nested mounts, no unmounting, no mount-time validation that a MiniFS
  mount actually points at a formatted filesystem (a `vfscat` against an
  unformatted `/system` just fails the same way a missing file would,
  indistinguishable from "not found"). Paths are also not normalized or
  bounds-checked beyond what `starts_with`/array indexing naturally do -
  no `..`, no double slashes handled specially, no path-length cap
  enforced before it's used (the shell's own 128-byte line buffer is the
  real limit in practice).
- `disk/devfs.mc` has exactly one pseudo-file (`/devices/ticks`) and is
  read-only - `vfs_write()` unconditionally fails for any `/devices` path
  rather than routing to a per-file write handler, since none of the
  live kernel state it currently exposes has a sensible "write" meaning.
  More device files (task count, free heap/frame counts, etc.) are
  straightforward additions of the same shape whenever something needs
  them.
- Mount dispatch is a tag (`backend`) plus `if`/`else`, not function
  pointers - C obviously supports real function pointers as struct
  fields/callbacks with no caveats, so this is now purely "hasn't been
  worth the refactor yet" rather than a language-capability question.
  Worth revisiting once a third+ backend makes the if/else chain
  genuinely unwieldy.
- `File.write()`'s syscall return value has been observed to
  intermittently read back as `-1` (`0xffffffffffffffff`) even though
  the write itself demonstrably succeeds - a subsequent `File.read()` or
  the shell's own `ls` in the same session consistently shows the file
  present with the correct byte count every time this has been seen.
  First observed during the post-C-rewrite regression pass (not known
  to have occurred in the MiniC-era kernel, though it may simply never
  have been exercised under the same timing). Not reproducible on
  demand - seen in roughly half of a handful of boots, including at
  least one with only a single ring3 process running (so not purely a
  multi-process ATA-controller-contention story, though `disk/ata.c`
  genuinely has no locking around the shared 0x1F0-0x1F7 port range and
  that remains a real, separate gap worth closing regardless). Data
  integrity is unaffected in every observed case; only the reported
  result is occasionally wrong. Needs the project's own "add diagnostics,
  reason from what they show" treatment in a future session rather than
  a guessed fix here.
- `net/tcp.c` is a client only (no listening/server side), one
  connection at a time (no connection table - a second concurrent
  `tcp_fetch()` call would stomp the first's state), and has no
  retransmission or congestion control at all - a single lost segment
  anywhere in the exchange just times out via the same tick-bounded
  budget every other protocol layer in this phase uses, rather than
  being retried. No TCP options either (no window scaling, no SACK, no
  MSS negotiation - `TCP_WINDOW` is a fixed, generous constant that's
  never actually been tested against a real flow-control squeeze). Close
  is best-effort: if the peer never sends a FIN, the connection is simply
  abandoned once the milestone's own success criterion (a real handshake
  plus real data) is already met - nothing tracks this as a leak, since
  there's no connection table for it to leak out of in the first place.
