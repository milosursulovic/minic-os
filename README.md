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
MiniC through milestone 34, then rewritten by hand into C afterward for
faster development — the "no external libraries, everything hand-written"
rule didn't change, only the implementation language did. See
[os-docs's roadmap](https://minic-os-docs.milosursulovic2696.workers.dev/roadmap#rewrite) for that story in
full, and the git history for one commit per rewrite stage.

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
  init.c             milestone 36: a real init process - spawns hello_service.c
  hello_service.c    milestone 36: a trivial real service, spawned by init.c
  ring3.ld           standalone linker script (keeps sections contiguous)
  ring3blob.s / init_blob.s / hello_service_blob.s
                     each wraps its own objcopy'd flat blob for the loader
  process.c/.h       spawn_process()/spawn_process_from_path() - the real loader
  object.c/.h        kernel object table + per-process handle tables (rights)
  channel.c/.h       IPC channels
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

44 milestones shipped, spanning boot → interrupts → heap/paging →
scheduler → syscalls/ring3 → per-process isolation → a native File/
Channel/Process API + POSIX shim → capability/permission hardening →
PCI/NIC/ARP/IP/UDP/DNS/TCP networking → a real init process → real
process exit → process supervision → frame reclamation on exit →
process/task slot reuse → kernel object reclamation on exit → early
handle release → runtime service registration → service
unregistration. The first 34 were built in MiniC; the kernel was then
rewritten by hand into C (see the note at the top of this file).
Milestone 44 adds syscall 15 (`unregister_service`): the fixed 4-slot
runtime registry milestone 43 added can now be freed and reused,
closing that milestone's own deliberately-deferred gap - the one
resource this kernel still couldn't reclaim. Verified in QEMU, twice:
`register_service()` returns index `0x1`; `unregister_service(0x1)`
succeeds; a `spawn_builtin(0x1)` attempt right afterward correctly
fails (`-1`), confirming the slot is genuinely freed, not just marked;
registering again returns the *same* index `0x1` - real reuse, not
just a second slot - and `spawn_builtin`ing that reused index launches
a real process whose own self-query matches. `objs` still lands on the
same `0x5` milestone 43 already established, confirming the
register/unregister/re-register cycle itself doesn't touch the
(unrelated) kernel object table. Milestone 43 adds syscall 14
(`register_service`): a ring3 process can load a VFS file into a
runtime registry slot and get back an index `spawn_builtin` (syscall
11) can launch, alongside the one fixed compile-time entry - the
registry isn't only the compile-time table anymore. Verified in QEMU,
twice: the boot-time demo process registers its own just-`install`ed
binary (`/system/testprog.bin`) at runtime, gets back index `0x1` (the
first dynamic slot; index `0` stays reserved for the built-in
`hello_service`), and `spawn_builtin`s it - the resulting process's own
self-query independently reports the exact same `task_index`
`spawn_builtin` returned, and it runs its full self-test sequence
(File/POSIX/Channel) exactly like any other loaded instance, proving
the runtime-registered image is genuinely running, not a stub. The
existing kernel-mode debug shell (`help`/`frames`/`tasks`/`pci`/... -
most of it touching raw kernel internals no real design should expose
to arbitrary userspace code directly) deliberately stays exactly as it
is; migrating it isn't the next step.

See [os-docs's Roadmap](https://minic-os-docs.milosursulovic2696.workers.dev/roadmap) for the full
milestone-by-milestone history with real captured verification output
for every one of them.

## Known limitations (on purpose, for now)

- A process can exit (`process_exit`, milestone 37), its private-region
  frames and PML4/PDPT are freed on exit (milestone 39), its
  process/task table slot is reused by the next spawn instead of
  growing the table forever (milestone 40 - the reused slot's kernel
  stack is reinitialized in place too, sidestepping "freeing your own
  currently-executing stack from within itself" entirely rather than
  solving it), and its own handle table's objects are freed too
  (milestone 41 - `alloc_object`/`alloc_handle` already searched for a
  free slot before appending, they just never had anything freed to
  find until now). A process can also now free one of its own handles
  early, without exiting, via `handle_close` (milestone 42, syscall
  13) - the fix for the narrower gap milestone 41 left open: a handle
  one process holds *to* another (e.g. `init`'s own `open_process`
  handle onto `hello_service`) lives in the *holder's* table, so only
  the holder closing it early or exiting ever frees it, never the
  target's own exit. Any handle still pointing
  at an exited-then-reused process slot also now points at a
  *different, live* process, not just a frozen dead one - a sharper,
  more real version of the same "no ownership on handles" gap below.
- Pointer arguments (paths, buffers) are checked for validity/bounds
  but not ownership - nothing stops a ring3 process from passing a
  pointer that doesn't actually belong to it.
- Every fixed-size table (tasks, processes, objects, handles, channels,
  mounts) has a small, arbitrary capacity, and most boot-time creation
  calls don't check their own return value.
- `register_service` (milestone 43) has its own small, fixed capacity -
  4 runtime registry slots, 16KB each. `unregister_service` (milestone
  44) can free one for reuse, but doesn't check that the caller is the
  one who registered it - any process can unregister any slot,
  matching the existing pointer-ownership gap above rather than closing
  it. Unregistering doesn't affect processes already spawned from that
  slot, since a process's image is copied into its own address space
  at spawn time, not referenced from the registry afterward.
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
- `spawn_builtin` (syscall 11) can only launch one of a small, fixed,
  compile-time-embedded set of programs (one, today: `hello_service.c`)
  by index - there's no way for init or anything else to register a new
  one at runtime, and nothing yet bridges "a real service lives on disk"
  to this mechanism (that's still `spawn_process_from_path()`/syscall 6,
  a separate path). `init.c` restarts its one service exactly once
  (milestone 38) - a real restart-on-exit proof, but not a real
  supervisor: no restart limit/backoff, no dependency ordering, and a
  second restart would fail outright anyway once the fixed 4-slot
  process table fills up (see the next bullet).
- `File.write()`'s syscall return value has been observed to
  intermittently read back as `-1` even though the write itself
  demonstrably succeeds (a subsequent read/`ls` always shows the
  correct file) - not reproducible on demand, data integrity is never
  affected, needs real diagnostics in a future session.

See [os-docs's Known Limitations](https://minic-os-docs.milosursulovic2696.workers.dev/reference#limitations)
for the fuller list with links to exactly which milestone closed each
now-fixed gap.

## Docs

This file is deliberately a short overview. The
[os-docs site](https://minic-os-docs.milosursulovic2696.workers.dev/) (repo `minic-os-docs`) carries
the real depth: [Getting Started](https://minic-os-docs.milosursulovic2696.workers.dev/install),
[Shell Guide](https://minic-os-docs.milosursulovic2696.workers.dev/guide),
[Architecture reference](https://minic-os-docs.milosursulovic2696.workers.dev/reference),
[an annotated real session walkthrough](https://minic-os-docs.milosursulovic2696.workers.dev/examples), and
the [full milestone roadmap](https://minic-os-docs.milosursulovic2696.workers.dev/roadmap). `CLAUDE.md` in
this repo carries the load-bearing architecture notes worth knowing
before touching `boot.s`/`interrupts.s`/paging/scheduling code, and the
exact toolchain mechanics behind this kernel's build.
