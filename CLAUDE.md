# MiniC-OS kernel

A multiboot1 kernel written in hand-written, freestanding C (plus a handful of hand-written `.s` files for exactly the things below what C can express: boot/long-mode transition, interrupt entry stubs, context switching, ring3 entry). Boots via QEMU's `-kernel` shortcut for fast iteration, or a real GRUB-bootable ISO (`./build.sh iso`) for VirtualBox/VMware/real hardware.

This repo's sibling docs repo is `../minic-os-docs` (the deployed docs site) — every milestone gets a matching "Document X" commit there; see the `kernel-milestone` skill for the sync checklist. (`net-desk` and `vnc`, if present alongside this repo, are unrelated projects that just happen to share the same parent folder — not part of this workspace.) The kernel used to be written in a hand-rolled MiniC dialect and built against a separate `minicc` compiler repo (`compiler/`); both were retired in favor of writing the kernel directly in C for faster iteration (the language, not the design philosophy, changed — see below). `README.md`'s roadmap documents the migration as a real, dated event, not rewritten history.

## No external libraries — everything is hand-written

Every subsystem in this kernel is implemented from scratch in C (or hand-written `.s` for what's genuinely below what C can express — boot/interrupt entry, context switching, ring3 entry). Never link against, port in, or adapt an existing library or reference implementation — no borrowed drivers, no FAT32/ext2 implementation ported in, no lwIP-style TCP/IP stack, no compression/crypto library. The only exception is the toolchain itself (`gcc`/`as`/`ld`/`objcopy`) and the freestanding-guaranteed standard headers (`<stdint.h>`, `<stdbool.h>`, `<stddef.h>` — compiler-provided type definitions, not a linked runtime). This has been true for every milestone so far (heap, scheduler, paging, every driver including the ATA PIO one and the e1000 NIC) and applies just as much to what's still ahead — TCP (milestone 35) and the capability/security phase after it.

## Building

```bash
./build.sh          # kernel.elf
./build.sh iso       # + minic-os.iso
./build.sh disk      # + disk.img (regenerated from scratch)
```

(Native Linux — no WSL/`/mnt` wrapping needed; run these directly from the repo root.)

`build.sh` is a thin wrapper over a real `Makefile` (`make`, `make run`, `make iso`, `make disk`) — every `.c` file compiles to its own object with real incremental rebuilds. Needs `gcc`/`as`/`ld`/`objcopy` (any recent GCC toolchain; developed against gcc 15). `./build.sh iso` additionally needs `grub-mkrescue`/`xorriso`/`mtools` (`sudo apt install grub-pc-bin grub-common xorriso mtools`).

**Why a 32-bit ELF container for 64-bit code**: QEMU's multiboot1 `-kernel` loader hard-rejects a genuine ELF64 image ("Cannot load x86-64 image, give a 32bit one") — confirmed empirically, not assumed, when this build was set up. So every object still links as `ld -m elf_i386` even though the code inside runs in real 64-bit long mode: each `.c` file compiles to assembly (`gcc -S`), gets a literal `.code64` directive prepended, and is fed to `as --32`, which then encodes the 64-bit mnemonics correctly while keeping the ELF32 container format. `boot.s`/`interrupts.s`/`switch.s`/`usermode.s` (hand-written, unchanged from the MiniC era) already did this; the Makefile just runs the same pipeline for gcc's output.

**Two gcc flags this pipeline specifically needs, beyond the obvious freestanding ones**: `-fPIC` (without it, gcc materializes a string/global's absolute 64-bit address as a sign-extended 32-bit immediate needing relocation type `R_X86_64_32S`, which isn't representable in an ELF32 relocation table — `as --32` rejects it outright) and `-fvisibility=hidden` (even with `-fPIC`, a reference to any externally-linkable symbol still routes through the GOT via `sym@GOTPCREL(%rip)`, an ELF64-only relocation syntax `as --32` can't parse — hidden visibility is correct anyway, since this is one statically-linked image, never dynamically linked). Because the CLI flag only covers symbols *defined* in a given translation unit, every header additionally wraps its declarations in `#pragma GCC visibility push(hidden)` / `pop` — the standard template for any new header in this codebase.

A `LOAD segment with RWX permissions` warning from `ld` while linking `proc/ring3prog_linked.elf` is expected — that standalone link's only purpose is to get `objcopy -O binary`'d into a flat blob immediately afterward (see `proc/ring3.ld`), so the intermediate ELF's segment permission bits are never actually enforced by anything and don't matter.

## Testing — always in QEMU, always with a concrete assertion

**Load the `kernel-qemu-test` skill before writing ad hoc QEMU commands** — it has the exact working recipe plus several real gotchas (keystroke word-splitting silently eating spaces, command batching sometimes hanging, QEMU/TCG's timer running far faster than the nominal 100Hz which breaks naive timing comparisons across separate commands) that each cost real debugging time to discover. Delegate a verification run to the `kernel-qemu-tester` agent when it's confirmation, not active debugging.

**"Didn't crash" is never sufficient verification in this codebase.** Every milestone here has a concrete, checkable assertion: exact byte counts matching hand computation, a register value read back and printed (e.g. `cs=0x1B` proving real CPL 3, not just "a syscall happened"), a counter ratio matching an expected formula, a frame-allocator count identical before/after an operation that shouldn't leak. Design the verification *before* declaring a milestone done, the same way every one from milestone 1 onward has.

## Project layout

One folder per subsystem — see `README.md`'s "Project layout" section for the full breakdown (`boot/`, `drivers/`, `mm/`, `lib/`, `isr/`, `sched/`, `syscall/`, `proc/`, `disk/`, `net/`, `shell/`). Every subsystem is a `.c`/`.h` pair (C has no whole-program `import` merge the way MiniC did, so each file's public surface is declared in its own header). `kmain.c` at the root is just the entry point plus the `#include`s wiring everything together.

## Architecture notes worth knowing before touching this

- **32-bit ELF container, 64-bit code.** Every hand-written `.s` file — and every gcc-compiled `.c` file via the Makefile's own pipeline — assembles with `as --32` (multiboot1 needs an ELF32 container) even though its code runs in 64-bit long mode via `.code64` directives. This means: no `mov rsp, <label>` in hand-written asm (needs an unrepresentable 64-bit relocation — use `mov esp, <label>` instead, which zero-extends), no `.quad <label>` for a full 64-bit address as static data (split into two `.long`s, low half + a static `0` high half, since this kernel always loads well under 4GB), and any bitwise/shift expression directly on a cross-section symbol (`label >> 16`, etc.) won't assemble — patch those fields with real instructions at boot instead.
- **Every hand-written-asm reference to a C global uses `[rip+label]`, never bare `[label]`** — same relocation-truncation reasoning as gcc's own `-fPIC` codegen.
- **TSS.RSP0 is an absolute reset point, not a continuation.** The CPU loads it fresh on *every* ring3→ring0 transition — pointing it at a stack something else is already using will silently corrupt that other thing's state the next time an interrupt fires. (Real milestone-11 bug, cost real debugging time — see the kernel's own commit history / project memory for the story.)
- **A GP fault with error code 0** means "not tied to a specific bad selector" — consistent with a bad `ret`/jump target rather than a segment-load problem. Useful diagnostic signature.
- **A loaded program's entry point is not necessarily byte 0 of its compiled image.** `spawn_process()` (`proc/process.c`) always jumps to the start of the loaded byte range - true by construction for the old MiniC compiler's simplistic single-pass codegen (which always placed the entry function first), but NOT true for gcc, which lays out `.text` in whatever order suits it. `proc/ring3prog.c`'s `_start()` is pinned to offset 0 via a dedicated `__attribute__((section(".text.start")))` plus `proc/ring3.ld` pulling that section in first — a real bug found and fixed during the C rewrite (the CPU ran cold into a different function's body, whose balanced-but-entry-less prologue/epilogue eventually made `ret` read from the exact top-of-stack address). Any *new* standalone-linked, flat-blob-loaded program needs the same section pin, not just "whichever function happens to be declared first."
- **The whole static identity map is currently user-accessible** (a deliberate, temporary simplification from early in the ring3 work) — real per-process isolation exists (`clone_address_space()`), but nothing has gone back to tighten the identity map itself. Don't treat "ring3 code can't touch X" as true for an address inside that identity-mapped region without checking.
- **The scheduler's `switch_context`/`run_ring3_test` save-rsp/pop/ret trick** is the one mechanism this whole kernel uses for "suspend here, resume later from somewhere else entirely" — cooperative task switching, preemption (called from inside the timer ISR), and the one-shot ring3 entry all reuse the exact same pattern. Understand it once, recognize it everywhere.
- **`sleep_ticks()`'s blocked-task scan relies on task 0 (the shell) never blocking itself** — that's what guarantees `yield()` always finds something runnable. Not enforced anywhere; a future task that blocks without this guarantee needs a real idle task.
- **`interrupt_handler()`'s third argument is the interrupted context's saved RIP** — read directly off the trap frame by `boot/interrupts.s`'s `isr_common_stub` before it pushes anything else. Printed alongside the fault address/error code for GPFs and page faults; genuinely useful for pinning down which instruction actually faulted rather than guessing from the address alone (this is exactly what made the `spawn_process()` entry-point bug above obvious instead of a guess).

## Docs

`README.md`'s roadmap section gets a full update in the same session a milestone ships. `../os-docs/` (the kernel's own docs site — getting started, shell guide, architecture, walkthrough, roadmap) gets the real, detailed sync. See the `kernel-milestone` skill for the exact checklist.
