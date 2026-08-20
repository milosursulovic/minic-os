# MiniC-OS kernel

A multiboot1 kernel that boots to 64-bit long mode, handles real
interrupts, has a real heap and physical frame allocator, dynamic
paging with per-process address-space isolation, a preemptive
scheduler, a real ring0/ring3 syscall boundary with a kernel object/
handle-table capability model, IPC channels, a disk driver with a
custom filesystem and a small VFS, a native File/Channel/Process API
with a POSIX-shaped shim on top, and a real networking stack (PCI
enumeration, an e1000 NIC driver, ARP, IPv4/ICMP, UDP/DNS, and TCP —
verified with a genuine HTTP round trip to a real internet host). Every
subsystem is implemented from scratch, verified running in QEMU with a
concrete, checkable assertion each time — never just "it didn't crash."

Written in hand-written, freestanding C (plus a handful of hand-written
`.s` files for exactly what's below what C's inline asm can express:
boot/long-mode transition, interrupt entry, context switching, ring3
entry). This kernel was originally built in a custom language called
MiniC, then rewritten by hand into C partway through development for
faster iteration — the "no external libraries, everything hand-written"
rule didn't change, only the implementation language did. See the git
history for the rewrite, one commit per stage.

## Project layout

```
types.h           u8-u64/i8-i64 typedefs, plus bool/NULL - included almost everywhere
kmain.c           entry point (_start) + #includes wiring everything together
boot/             hand-written assembly - below what inline asm can express
  boot.s            multiboot header, 32-to-64-bit transition, GDT/TSS
  interrupts.s       ISR/IRQ entry stubs (save/restore, call into C)
  linker.ld          places the multiboot header + code at the 1MB load address
drivers/          hardware setup and I/O
  io.c/.h            VGA text buffer, serial port, raw port I/O
  interrupts_init.c/.h  IDT + 8259 PIC remap + PIT reconfiguration
  keyboard.c/.h      scancode table + the shell's line buffer
  pci.c/.h           PCI bus enumeration (legacy CONFIG_ADDRESS/CONFIG_DATA)
net/              networking
  e1000.c/.h         the e1000 NIC driver - PCI enable, MMIO registers, TX/RX rings
  arp.c/.h           a real ARP resolver with a cache
  ip.c/.h            IPv4 header build + checksum, this kernel's "my IP"
  icmp.c/.h          ICMP echo (ping)
  udp.c/.h           UDP send/receive + pseudo-header checksum
  dns.c/.h           a minimal hand-crafted DNS query/resolver
  tcp.c/.h           a real TCP client - handshake, data, best-effort close
mm/               memory management
  heap.c/.h          kalloc/kfree free-list allocator, grows on demand
  frames.c/.h        multiboot memory map parser + physical frame allocator
  paging.c/.h        dynamic PML4/PDPT/PD/PT paging, per-process address spaces
lib/              no-libc helpers
  strings.c/.h       streq/starts_with/strlen_/parse_hex/print_hex/format_hex
isr/              interrupt dispatch
  isr.c/.h           interrupt_handler, called from interrupts.s's stubs
sched/            preemptive task scheduler
  switch.s           hand-written context switch (below what inline asm can express)
  task.c/.h          task table, create_task/yield/sleep_ticks/channel_receive
syscall/          ring0/ring3 boundary
  usermode.s         hand-written ring3 entry (below what inline asm can express)
  syscall.c/.h       syscall dispatcher: print, handle-query, File/Channel/Process
proc/             process loading + the kernel object model + IPC
  ring3prog.c        the loaded ring3 "program" - real C, compiled standalone
  init.c             a real init process - spawns hello_service.c, supervises it
  hello_service.c    a trivial real service, spawned by init.c
  ring3.ld           standalone linker script (keeps sections contiguous)
  ring3blob.s / init_blob.s / hello_service_blob.s
                     each wraps its own objcopy'd flat blob for the loader
  process.c/.h       spawn_process()/spawn_process_from_path() - the real loader
  object.c/.h        kernel object table + per-process handle tables (rights)
  channel.c/.h       IPC channels
  io_request.c/.h    async file reads/writes - a dedicated worker task
                     + a fixed pool of pending-request slots
  net_request.c/.h   async ICMP ping + DNS resolve - their own worker
                     task + slot pool, separate from io_request's so
                     one domain never stalls the other
disk/             storage
  ata.c/.h           legacy ATA PIO driver - real sector read/write
  minifs.c/.h        MiniFS - a minimal custom filesystem
  vfs.c/.h           path -> mount -> backend routing
  devfs.c/.h         the "/devices" backend - live kernel state, no disk
shell/            the interactive shell
  shell.c/.h         cmd_* functions + run_command dispatch
```

See [os-docs's Architecture reference](https://minic-os-docs.milosursulovic2696.workers.dev/reference) for
the deep dive on every subsystem above - boot process, memory map,
scheduler, process model, IPC, the syscall ABI, capabilities, the
native API, the POSIX shim, and every driver/protocol layer, each with
real captured verification output.

## Building and running

Needs `qemu-system-x86_64` (`sudo apt install qemu-system-x86` on
Debian/Ubuntu/WSL) and a real GCC toolchain (`gcc`/`as`/`ld`/`objcopy` -
developed against gcc 15, any recent GCC works). No sibling checkout of
anything else is needed.

```bash
./build.sh          # runs `make`: compiles every .c, assembles every .s, links kernel.elf
./build.sh run       # also boots it in QEMU (curses display, in-terminal), with a disk attached
./build.sh iso       # also packages a GRUB-bootable minic-os.iso
./build.sh disk      # (re)builds disk.img - a 1MB test disk image, gitignored
```

`build.sh` is a thin wrapper over a real `Makefile` - every `.c` file
compiles to its own object with real incremental rebuilds (`gcc -S` to
assembly, a `.code64` directive prepended, `as --32` to an ELF32 object
- QEMU's multiboot1 loader rejects a genuine ELF64 image outright, even
though the code inside runs in real 64-bit long mode; see `CLAUDE.md`
for the full toolchain mechanics). `./build.sh run` builds `disk.img`
automatically if it isn't already there and attaches it via QEMU's
`-drive`. Booting and every command except `disk`/`diskwrite` work
identically with or without it.

## Running outside QEMU (VirtualBox, VMware, real hardware)

QEMU's `-kernel kernel.elf` is a QEMU-only shortcut - nothing else
understands multiboot1 directly, so anywhere else needs a real
bootloader in front of the same `kernel.elf`. Since the kernel is
already multiboot1-compliant, GRUB2 chainloads it directly - no
kernel-side changes needed, just packaging.

`./build.sh iso` needs `grub-mkrescue`/`xorriso`/`mtools`
(`sudo apt install grub-pc-bin grub-common xorriso mtools`). For
VirtualBox: create a VM (type "Other", 64-bit, no EFI, no guest
additions needed), attach `minic-os.iso` as the optical drive and
(optionally, for disk commands) `disk.img` as a plain IDE hard disk,
and boot - GRUB's menu appears and boots straight into the kernel.
VMware and real hardware (via a USB stick) should work the same way,
unverified so far.

To check output without a display, redirect the serial port to a file:

```bash
qemu-system-x86_64 -kernel kernel.elf -display none -serial file:serial.log -no-reboot
```

See [os-docs's Getting Started guide](https://minic-os-docs.milosursulovic2696.workers.dev/install) for the
full walkthrough, and [the Shell Guide](https://minic-os-docs.milosursulovic2696.workers.dev/guide) for
every command.

## Current status

The kernel supports a full process lifecycle. A process can be spawned
from a VFS file path, from one of a small set of fixed compile-time-
embedded programs, or from a program registered at runtime
(`register_service`, backed by 4 runtime registry slots) - all three
routes go through the same loader, the same syscall boundary, and the
same per-process address-space isolation. On exit, a process's private
memory (its loaded image, stack, and page-table frames, plus its own
PML4/PDPT), its own kernel-object handle table, and its process/task
table slot are all freed and become available for reuse by the next
spawn - none of these fixed-size kernel tables grow without bound
across repeated spawn/exit cycles. A process can also release one of
its own handles early, without exiting (`handle_close`), and a
runtime-registered service can be unregistered and its slot reused
(`unregister_service`) independently of any process's own lifecycle.

`init.c` is a real init process: it spawns a service, polls it until
it exits, and restarts it - genuine supervision, not spawn-and-forget.

File reads and writes can also be genuinely asynchronous: a process
issues one and gets back a handle immediately, does other real work of
its own choosing, then collects the result whenever it actually needs
it - it isn't forced to sit idle for the operation's own duration the
way most I/O in this kernel still is. A dedicated kernel worker task
performs the real disk read or write concurrently (the ATA driver is
PIO-only, so this is cooperative multitasking doing the work real
DMA/interrupts would elsewhere, not hardware asynchrony), and the
caller only actually blocks - via the same scheduler wake-condition
mechanism blocking IPC receive already uses, not a busy spin - once it
asks to wait for a result that isn't ready yet.

Ring3 code can also do real networking now, for the first time -
asynchronously from the start: a process can issue a real ICMP ping or
a real DNS resolve and get back a handle immediately, the same shape
as the file operations, backed by their own separate worker task so a
slow network operation can't stall a pending file operation or vice
versa. Every network wait loop this kernel already had (ARP
resolution, ICMP's own reply poll, UDP's own reply poll that DNS sits
on top of) now yields cooperatively while waiting too, not just
relying on the timer to force a switch eventually.

All of this is verified in QEMU with exact, checkable arithmetic
throughout, not just "it didn't crash" - the kernel object table's
live count matches hand computation after a spawn/exit/restart cycle,
a service registered at runtime, unregistered, and re-registered
reproducibly gets back the exact same registry slot rather than a
fresh one, an async file read's own result - byte count and content
both - exactly matches what a synchronous read of the same file already
returned moments earlier in the same boot, an async write's own result
is independently confirmed by reading the file straight back afterward,
an async ping genuinely reaches QEMU's real gateway and gets back a
matching reply, and an async DNS resolve gets back a real IP for a
real hostname - cross-checked against the existing kernel-mode TCP
demo's own independent DNS resolution of the same hostname in the same
boot, both landing on the identical address. Several real prints from
the calling process's own continued execution land in between issuing
each operation and waiting for it, proving it
genuinely wasn't blocked. The existing kernel-mode debug shell
(`help`/`frames`/`tasks`/`pci`/... - most of it touching raw kernel
internals no real design
should expose to arbitrary userspace code directly) deliberately stays
exactly as it is.

See [os-docs's Capabilities overview](https://minic-os-docs.milosursulovic2696.workers.dev/roadmap) for
a subsystem-by-subsystem breakdown with real captured verification
output for everything above.

## Known limitations (on purpose, for now)

- A process can exit (`process_exit`), and everything tied to it is
  genuinely reclaimed: its private-region frames and PML4/PDPT are
  freed, its process/task table slot is reused by the next spawn
  instead of growing the table forever (the reused slot's kernel stack
  is reinitialized in place too, sidestepping "freeing your own
  currently-executing stack from within itself" entirely rather than
  solving it), and its own handle table's objects are freed with it. A
  process can also free one of its own handles early, without exiting,
  via `handle_close` - needed because a handle one process holds *to*
  another (e.g. `init`'s own `open_process` handle onto
  `hello_service`) lives in the *holder's* table, so only the holder
  closing it early or exiting ever frees it, never the target's own
  exit. One sharper edge remains: any handle still pointing at an
  exited-then-reused process slot now points at a *different, live*
  process, not just a frozen dead one - a sharper version of the same
  "no ownership on handles" gap below.
- Pointer arguments (paths, buffers) are checked for validity/bounds
  but not ownership - nothing stops a ring3 process from passing a
  pointer that doesn't actually belong to it.
- Every fixed-size table (tasks, processes, objects, handles, channels,
  mounts) has a small, arbitrary capacity, and most boot-time creation
  calls don't check their own return value.
- `register_service` has its own small, fixed capacity - 4 runtime
  registry slots, 16KB each - though `unregister_service` can free one
  for reuse. `unregister_service` doesn't check that the caller is the
  one who registered the slot - any process can unregister any slot,
  matching the pointer-ownership gap above rather than closing it.
  Unregistering doesn't affect processes already spawned from that
  slot, since a process's image is copied into its own address space
  at spawn time, not referenced from the registry afterward.
- Async file I/O is backed by a fixed pool of 4 pending-request slots
  with a 512-byte buffer each - a read or write payload larger than
  that gets silently truncated to the buffer's capacity, same as any
  other fixed-size table in this kernel. Async networking covers ICMP
  ping and DNS A-record resolution only, from a separate 2-slot pool -
  no async ARP/UDP/TCP for ring3 yet, and every one of those still has
  no ring3-facing syscall at all (they remain kernel-mode shell
  commands only, same as before).
- A loaded ring3 program's own code+data image is fully executable (no
  W^X split within it - the loader has no tracked code/data boundary).
  No ASLR, no sandboxing beyond address-space isolation.
- MiniFS is deliberately simple: flat namespace, a fixed 16-file cap,
  no delete/truncate/append, no journaling, no safe overwrite.
- The ATA driver only supports the primary bus's master drive, polling
  rather than interrupt-driven.
- PCI enumeration is bus 0 only, no bridge recursion.
- ARP is client-only (no responder), with no cache eviction/TTL. This
  kernel's own IP address is a fixed, static assumption, not real
  DHCP-negotiated configuration.
- ICMP only implements echo request/reply. `dns_query()`/`dns_resolve_a()`
  are deliberately not a real DNS client (no caching, no retries, A
  records only).
- TCP (`net/tcp.c`) is client-only, one connection at a time with no
  connection table, no retransmission or congestion control (a lost
  segment just times out), no options (no window scaling, no SACK), and
  close is best-effort only.
- The e1000 TX/RX rings are fixed-size (8 descriptors) and poll rather
  than using the device's own interrupt capability, same as the ATA
  driver.
- `spawn_builtin` (syscall 11) launches a program by registry index -
  either the one fixed compile-time entry (`hello_service.c`) or a
  runtime-registered slot (see `register_service` above) - never a raw
  pointer. `init.c` restarts its one service exactly once - a real
  restart-on-exit proof, but not a real supervisor: no restart
  limit/backoff, no dependency ordering, and a second restart would
  fail outright once the fixed 4-slot process table fills up.
- `File.write()`'s syscall return value has been observed to
  intermittently read back as `-1` even though the write itself
  demonstrably succeeds (a subsequent read/`ls` always shows the
  correct file) - not reproducible on demand, data integrity is never
  affected, needs real diagnostics in a future session.

See [os-docs's Known Limitations](https://minic-os-docs.milosursulovic2696.workers.dev/reference#limitations)
for the fuller list, including gaps already closed and how.

## Docs

This file is deliberately a short overview. The
[os-docs site](https://minic-os-docs.milosursulovic2696.workers.dev/) (repo `minic-os-docs`) carries
the real depth: [Getting Started](https://minic-os-docs.milosursulovic2696.workers.dev/install),
[Shell Guide](https://minic-os-docs.milosursulovic2696.workers.dev/guide),
[Architecture reference](https://minic-os-docs.milosursulovic2696.workers.dev/reference),
[an annotated real session walkthrough](https://minic-os-docs.milosursulovic2696.workers.dev/examples), and
a [capabilities overview](https://minic-os-docs.milosursulovic2696.workers.dev/roadmap). `CLAUDE.md` in
this repo carries the load-bearing architecture notes worth knowing
before touching `boot.s`/`interrupts.s`/paging/scheduling code, and the
exact toolchain mechanics behind this kernel's build.
