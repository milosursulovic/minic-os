# MiniC OS kernel

A multiboot1 kernel written in MiniC (plus a handful of hand-written `.s` files for exactly the things below what `asm(...)` can express: boot/long-mode transition, interrupt entry stubs, context switching, ring3 entry). Boots via QEMU's `-kernel` shortcut for fast iteration, or a real GRUB-bootable ISO (`./build.sh iso`) for VirtualBox/VMware/real hardware.

This repo is one of three siblings under `d:\Projects\minic` — see `../CLAUDE.md` for the multi-repo layout and the git-discipline rule. This repo depends on `../compiler/` existing as a sibling checkout (`build.sh`'s default `MINICC` path assumes it).

## Building

```bash
wsl.exe -e bash -c "cd /mnt/d/Projects/minic/os && ./build.sh"        # kernel.elf
wsl.exe -e bash -c "cd /mnt/d/Projects/minic/os && ./build.sh iso"    # + minic-os.iso
```

Needs a Linux-built `minicc` (`../compiler/build-linux/minicc` by default — rebuild that first if the compiler changed). `./build.sh iso` additionally needs `grub-mkrescue`/`xorriso`/`mtools` (`sudo apt install grub-pc-bin grub-common xorriso mtools`).

A `warning: unused function 'interrupt_handler'`/`'syscall_dispatch'` on every build is expected, not a bug — both are called only from hand-written assembly (`interrupts.s`), which `minicc` never parses, so it can't see the call. Same blind spot gcc's own `-Wunused-function` has for non-`static` functions.

## Testing — always in QEMU, always with a concrete assertion

**Load the `kernel-qemu-test` skill before writing ad hoc QEMU commands** — it has the exact working recipe plus several real gotchas (keystroke word-splitting silently eating spaces, command batching sometimes hanging, QEMU/TCG's timer running far faster than the nominal 100Hz which breaks naive timing comparisons across separate commands) that each cost real debugging time to discover. Delegate a verification run to the `kernel-qemu-tester` agent when it's confirmation, not active debugging.

**"Didn't crash" is never sufficient verification in this codebase.** Every milestone here has a concrete, checkable assertion: exact byte counts matching hand computation, a register value read back and printed (e.g. `cs=0x1B` proving real CPL 3, not just "a syscall happened"), a counter ratio matching an expected formula, a frame-allocator count identical before/after an operation that shouldn't leak. Design the verification *before* declaring a milestone done, the same way every one from milestone 1 onward has.

## Project layout

One folder per subsystem — see `README.md`'s "Project layout" section for the full breakdown (`boot/`, `drivers/`, `mm/`, `lib/`, `isr/`, `sched/`, `syscall/`, `shell/`). `kmain.mc` at the root is just the entry point plus the imports wiring everything together.

## Architecture notes worth knowing before touching this

- **32-bit ELF container, 64-bit code.** Every hand-written `.s` file assembles with `as --32` (multiboot1 needs an ELF32 container) even though its code runs in 64-bit long mode via `.code64` directives. This means: no `mov rsp, <label>` (needs an unrepresentable 64-bit relocation — use `mov esp, <label>` instead, which zero-extends), no `.quad <label>` for a full 64-bit address as static data (split into two `.long`s, low half + a static `0` high half, since this kernel always loads well under 4GB), and any bitwise/shift expression directly on a cross-section symbol (`label >> 16`, etc.) won't assemble — patch those fields with real instructions at boot instead.
- **Every MiniC-global reference from hand-written asm uses `[rip+label]`, never bare `[label]`** — same relocation-truncation reasoning as the compiler's own codegen.
- **TSS.RSP0 is an absolute reset point, not a continuation.** The CPU loads it fresh on *every* ring3→ring0 transition — pointing it at a stack something else is already using will silently corrupt that other thing's state the next time an interrupt fires. (Real milestone-11 bug, cost real debugging time — see the kernel's own commit history / project memory for the story.)
- **A GP fault with error code 0** means "not tied to a specific bad selector" — consistent with a bad `ret`/jump target rather than a segment-load problem. Useful diagnostic signature.
- **The whole static identity map is currently user-accessible** (milestone 11's deliberate, temporary simplification) — there is no real process isolation yet. Don't treat "ring3 code can't touch X" as true until milestone 12 (per-process address spaces) lands.
- **The scheduler's `switch_context`/`run_ring3_test` save-rsp/pop/ret trick** is the one mechanism this whole kernel uses for "suspend here, resume later from somewhere else entirely" — cooperative task switching, preemption (called from inside the timer ISR), and the one-shot ring3 exit all reuse the exact same pattern. Understand it once, recognize it everywhere.
- **`sleep()`'s blocked-task scan relies on task 0 (the shell) never blocking itself** — that's what guarantees `yield()` always finds something runnable. Not enforced anywhere; a future task that blocks without this guarantee needs a real idle task.

## Docs

`README.md`'s roadmap section, plus `../compiler/README.md`'s roadmap entry and `../docs/roadmap.html`, all get a synced update in the same session a milestone ships — see the `kernel-milestone` skill.
