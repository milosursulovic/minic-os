<p align="center">
  <img src="assets/icon.png" alt="MiniC-OS icon" width="128" height="128" />
</p>

# MiniC-OS

A multiboot1 x86-64 kernel with a real preemptive scheduler, per-process
paging isolation, a ring0/ring3 syscall boundary backed by a
capability/handle-table object model, a hand-written TCP/IP stack up
through TLS 1.2 and HTTP, a VFS with multiple real filesystem backends, a
windowed GUI with a widget toolkit, and a small desktop environment on
top - all hand-written, freestanding C (plus the handful of `.s` files
for exactly what's below what C can express: boot/long-mode transition,
interrupt entry, context switching, ring3 entry). No external libraries,
ever - see `CLAUDE.md` for the one narrow exception (the toolchain
itself) and the hard architecture rules. Every milestone is verified
running in QEMU against a concrete, checkable assertion - never just "it
didn't crash."

Originally written in a custom hand-rolled language (MiniC), rewritten
by hand into C partway through for faster iteration - the "everything
from scratch" rule never changed, only the implementation language did.

## What's real today

- **Kernel core**: interrupts/exceptions, a heap + physical frame
  allocator, dynamic per-process paging (copy-on-write `fork()`, guard
  pages, ASLR, NX), a preemptive round-robin scheduler with real threads.
- **Capability model**: a kernel object/handle-table system (Process,
  Thread, Channel, Pipe, SharedMemory, Socket, File, Directory, Device,
  Event, Mutex, Timer, IO_Request) with per-handle rights, plus a
  signed-executable + sandboxing layer (hand-written SHA-256/HMAC,
  syscall deny-bitmaps).
- **Filesystems**: a unified VFS mounting a custom filesystem (MiniFS), a
  real hand-written FAT32 backend (read+write, interoperates with
  standard tools), tmpfs, procfs/devfs, and POSIX-shaped
  `open/read/write/close/lseek/stat/unlink`.
- **Networking**: PCI enumeration, an e1000 NIC driver, ARP, DHCP,
  IPv4/IPv6 (NDP + SLAAC), ICMP/ICMPv6, UDP/DNS, TCP (client + server,
  real retransmission), and a from-scratch TLS 1.2 client (hand-written
  bignum/RSA, AES-128-CBC, X.509 parsing) with a real HTTP/1.1
  client+server on top - every layer verified against real, independent
  peers (a real internet host, a real `openssl` server, a real external
  client).
- **Graphics + GUI**: a linear framebuffer (VBE/DISPI), a window
  server with a Z-order compositor and real focus/keyboard routing, a
  widget toolkit (buttons, checkboxes, radios, sliders, lists, text
  boxes, progress bars), a real input event queue (keyboard with
  Shift/Ctrl/Alt, mouse buttons + scroll wheel, system-wide hotkeys), and
  a small desktop shell with a taskbar, launcher, terminal, file manager,
  device/service managers, and a settings app.
- **Drivers**: PS/2 keyboard + mouse (with real IntelliMouse wheel
  negotiation), ATA PIO disk, a hand-written UHCI USB controller driver
  (HID mouse/keyboard), CMOS RTC (isolated to its own ring3 driver
  process), PCI enumeration.
- **Users/permissions**: a real users/groups table, UID-based file
  ownership and permission bits enforced at the VFS layer.

## Project layout

```
types.h, kmain.c     shared typedefs; entry point wiring everything together
boot/                hand-written boot.s/interrupts.s/linker.ld
kernel/
  drivers/             keyboard, mouse, PCI, VBE, ATA, USB (UHCI), RTC, device manager
  mm/                  heap, physical frame allocator, paging, copy-on-write
  isr/                 interrupt dispatch (keyboard/mouse/timer/faults/hotkeys)
  sched/               preemptive scheduler, context switch, threads
  syscall/             ring0/ring3 boundary, one handler file per subsystem
  security/            SHA-256/HMAC, bignum/RSA, AES, X.509, exec signing, sandboxing, users
  net/                 arp, dhcp, ip, ipv6, ndp, icmp(6), udp, dns, tcp, tls, http, e1000
  fs/                  minifs, fat32, tmpfs, procfs, devfs, vfs
  gfx/                 window server, compositor, font, image/PNG, wallpaper
  services/            generic service manager (start/stop/restart/supervise)
proc/
  process.c/.h           the real ELF-less flat-blob loader (spawn_process*)
  ipc/                   channel, pipe, shared_memory, socket, event, mutex, timer, ...
  gui_toolkit/           ring3-side syscall wrappers + widgets, one header per subsystem
  posix/                 the POSIX-shaped shim over the native File API
  demo/, apps/           ring3 demo/test programs; the real desktop apps
    apps/                  desktop_shell, terminal, file_manager, settings,
                           device_manager, service_manager
shell/                 the kernel-mode debug shell (cmd_* per subsystem file)
```

See [os-docs](https://minic-os-docs.milosursulovic2696.workers.dev/reference) for the deep dive on
every subsystem - boot process, memory map, scheduler, capabilities, the
syscall ABI, and every driver/protocol layer with real captured
verification output.

## Building and running

Needs `qemu-system-x86_64` and a real GCC toolchain
(`gcc`/`as`/`ld`/`objcopy` - developed against gcc 15, any recent GCC
works).

```bash
./build.sh          # runs `make`: compiles every .c, assembles every .s, links kernel.elf
./build.sh run       # also boots it in QEMU (curses display, in-terminal), with a disk attached
./build.sh iso       # also packages a GRUB-bootable minic-os.iso
./build.sh disk      # (re)builds disk.img - a small test disk image, gitignored
```

`build.sh` is a thin wrapper over a real `Makefile` - every `.c` compiles
to its own object with real incremental rebuilds. QEMU's multiboot1
loader rejects a genuine ELF64 image outright, so each object still
links as ELF32 even though the code inside runs in real 64-bit long mode
(`gcc -S` -> a `.code64` directive prepended -> `as --32`); see
`CLAUDE.md` for the full toolchain mechanics.

`scripts/hw-info.sh` (Linux) / `scripts/hw-info.ps1` (Windows) collect a
read-only hardware report - useful when bringing this kernel up on real
hardware and something needs debugging against the actual host's
CPU/GPU/firmware.

## Running outside QEMU (VirtualBox, VMware, real hardware)

QEMU's `-kernel kernel.elf` is a QEMU-only shortcut - anywhere else needs
a real bootloader in front of the same `kernel.elf`. Since the kernel is
already multiboot1-compliant, GRUB2 chainloads it directly, no
kernel-side changes needed.

`./build.sh iso` needs `grub-mkrescue`/`xorriso`/`mtools`
(`sudo apt install grub-pc-bin grub-common xorriso mtools`). For
VirtualBox: create a VM (type "Other", 64-bit, no EFI), enable PAE/NX in
Processor settings, attach `minic-os.iso` as the optical drive and
(optionally) `disk.img`/`fat32.img` as plain IDE disks, and boot -
verified working. Real hardware boots the same ISO from a USB stick;
`scripts/hw-info.sh`/`.ps1` help debug anything that looks
hardware-specific (a real VBE/DISPI false-positive-on-real-GPU bug was
found and fixed this way).

To check output without a display, redirect the serial port to a file:

```bash
qemu-system-x86_64 -kernel kernel.elf -display none -serial file:serial.log -no-reboot
```

See [Getting Started](https://minic-os-docs.milosursulovic2696.workers.dev/install) and the
[Shell Guide](https://minic-os-docs.milosursulovic2696.workers.dev/guide) for the full walkthrough
and command reference.

## Known limitations (on purpose, for now)

- No audio, no clipboard/drag-and-drop, no package manager - every app
  hand-writes its own event loop (no declarative app framework yet).
- The GUI toolkit has no layout system and is missing
  Table/Menu/ContextMenu/TreeView/ScrollView.
- TLS trusts a server's certificate on first use (no chain-of-trust
  validation) and uses this kernel's own non-cryptographic RNG - a
  real-protocol/real-crypto interop proof, not a production security
  posture.
- Every fixed-size kernel table (tasks, processes, objects, handles,
  TCP connections, ...) has a small, arbitrary capacity.
- Font rendering is a fixed 5x7 bitmap face - no TrueType, Unicode, or
  anti-aliasing.

See [os-docs's Known Limitations](https://minic-os-docs.milosursulovic2696.workers.dev/reference#limitations)
for the fuller, current list.

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
