# Multiboot1 boot stub. GRUB/QEMU drop us in 32-bit protected mode; this
# file's entire job is to get to 64-bit long mode and hand off to
# MiniC-compiled code (`_start` in kmain.mc). This is real, standalone
# assembly, not MiniC - the multiboot header, GDT setup, page tables, and
# 32-to-64-bit transition are program *structure*, below what a
# function-body `asm("...")` statement can express. Every kernel project,
# even ones written in Rust/Zig, hand-writes an equivalent of this file.

.intel_syntax noprefix

# ---- Multiboot1 header - must land in the file's first 8KB, 4-byte aligned
.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00000003          # bit0: page-align modules, bit1: want a memory map
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot
.align 4
.long MB_MAGIC
.long MB_FLAGS
.long MB_CHECKSUM

# ---- Page tables + stack (BSS - zero-initialized, filled in at boot) -----
.section .bss
.align 4096
pml4:
    .skip 4096
pdpt:
    .skip 4096
pd:
    .skip 4096
.align 16
stack_bottom:
    .skip 16384
stack_top:

# Milestone 11: a TSS, needed so the CPU knows which kernel stack to load
# on a ring3->ring0 transition (RSP0) - without one, an interrupt firing
# while in ring3 has no valid stack to switch to. One shared TSS/RSP0 for
# now (there's no real per-process kernel stack yet - that's milestone
# 12+); this is the same "one hard problem at a time" scoping every
# milestone here has used.
#
# Deliberately its OWN stack, not stack_top above - TSS.RSP0 doesn't
# "continue" whatever was already on a stack, it's an absolute reset
# point the CPU loads fresh on *every* ring3->ring0 transition. Pointing
# it at stack_top would silently overwrite anything the kernel context
# that originally entered ring3 had already pushed there (found this the
# hard way: a ring3 test's own saved-registers trick got clobbered by
# the very first syscall it made, landing `ret` on garbage).
.align 16
int_stack_bottom:
    .skip 16384
int_stack_top:

.align 16
.global tss_start   # milestone 19: MiniC pokes RSP0 here directly per-task now (mm/paging.mc's setTssRsp0) - see its comment for why
tss_start:
    .skip 104
tss_end:

# ---- 32-bit entry point ---------------------------------------------------
.section .text
.code32
.global _boot_start
.extern _start              # MiniC's void _start(), in kmain.mc
.extern gMultibootInfoPtr   # MiniC global - see the note by its use below

_boot_start:
    mov esp, offset stack_top

    # Patch the TSS descriptor's base-address fields (see the comment by
    # gdt_start) - `tss_start`'s address is only usable here as a plain
    # value (`offset`), not inside the bitwise expressions the static
    # directives above needed and couldn't express.
    mov eax, offset tss_start
    mov [gdt_start + 40 + 2], ax   # base 0-15
    mov ecx, eax
    shr ecx, 16
    mov [gdt_start + 40 + 4], cl   # base 16-23
    shr ecx, 8
    mov [gdt_start + 40 + 7], cl   # base 24-31

    # Identity-map the first 1GB with 2MB pages: PML4[0] -> pdpt,
    # PDPT[0] -> pd, PD[0..511] each a 2MB page (PS bit set, no PT level
    # needed). Covers the kernel itself (loaded at 1MB) and the VGA text
    # buffer at 0xB8000 alike.
    #
    # Milestone 11: every level here also carries the user (0x04) bit now,
    # so ring3 code can execute/read the kernel image and ring3 test data
    # can live in ordinary .rodata - deliberately, temporarily coarse
    # (the *entire* static map becomes user-accessible, not just specific
    # pages). Real per-process isolation - only a process's own pages
    # being user-accessible - is milestone 12's job; nothing today relies
    # on this map being supervisor-only yet, so loosening it now doesn't
    # break any existing protection.
    mov eax, offset pdpt
    or eax, 0x07                # present + writable + user
    mov [pml4], eax
    mov dword ptr [pml4 + 4], 0

    mov eax, offset pd
    or eax, 0x07
    mov [pdpt], eax
    mov dword ptr [pdpt + 4], 0

    mov ecx, 0
.fill_pd:
    mov eax, ecx
    shl eax, 21                 # eax = ecx * 2MB
    or eax, 0x87                # present + writable + page-size (2MB) + user
    mov [pd + ecx*8], eax
    mov dword ptr [pd + ecx*8 + 4], 0
    inc ecx
    cmp ecx, 512
    jl .fill_pd

    mov eax, offset pml4
    mov cr3, eax

    # Enable PAE (CR4 bit 5) - required before long mode.
    mov eax, cr4
    or eax, 0x20
    mov cr4, eax

    # Set the Long Mode Enable bit (bit 8) and, as of milestone 28, the
    # No-Execute Enable bit (bit 11) too, in EFER (MSR 0xC0000080) - same
    # rdmsr/wrmsr round trip, since NXE must be live before ANY page
    # table entry anywhere ever sets the NX bit (PTE bit 63): with
    # NXE=0, that bit is a *reserved* bit instead, and setting it faults
    # on every access to the page (not just execution attempts) with a
    # reserved-bit violation - getting the ordering right here is what
    # makes every later NX-marked mapping (mm/paging.mc's PAGE_NX) safe.
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x900                # bit 8 (LME) | bit 11 (NXE)
    wrmsr

    # Enable paging (CR0 bit 31) - activates long mode, though we're still
    # running in a 32-bit code segment until the far jump below reloads CS
    # from the 64-bit descriptor.
    mov eax, cr0
    or eax, 0x80000000
    mov cr0, eax

    lgdt [gdt_ptr]
    jmp 0x08:_start64            # 0x08 = the code64 selector below

.code64
_start64:
    mov ax, 0x10                 # the data64 selector below
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov esp, offset stack_top   # 32-bit form: zero-extends into rsp, and (unlike
                                 # `mov rsp, ...`) only needs a 32-bit relocation -
                                 # everything here fits in 32 bits anyway (loaded at 1MB)

    # Milestone 11: reload the GDT a second time, now in 64-bit mode, so
    # the GDTR's limit covers the user/TSS descriptors appended below -
    # the first `lgdt` (still 32-bit at that point) only knew about the
    # original 3 entries. CS/SS etc. don't need reloading - entries 0-2
    # are unchanged in content and position, only the *limit* grew.
    lgdt [rip + gdt_ptr64]   # rip-relative, not plain [gdt_ptr64] - a bare
                               # absolute reference here needs a 64-bit-mode
                               # relocation type this file's 32-bit ELF
                               # container (multiboot1's constraint) can't
                               # represent at all; rip-relative uses an
                               # ordinary PC32 relocation instead, which it can

    # RSP0: the stack the CPU loads from the TSS on any ring3->ring0
    # transition (interrupt/exception firing while in ring3) - its own
    # dedicated stack, not stack_top; see int_stack_top's comment above.
    mov eax, offset int_stack_top
    mov dword ptr [rip + tss_start + 4], eax
    mov dword ptr [rip + tss_start + 8], 0

    mov ax, 0x28                 # TSS selector (GDT index 5)
    ltr ax

    # Multiboot handed us a pointer to its info structure in EBX at entry,
    # and nothing since has touched that register - stash it in a MiniC
    # global (a real, `.globl`-exported symbol, same trick as gOutPort/
    # gOutByte for outb) since `_start` takes no parameters.
    mov dword ptr [rip + gMultibootInfoPtr], ebx

    call _start

.hang:
    hlt
    jmp .hang

# ---- GDT: null, 64-bit code/data (ring0), 64-bit code/data (ring3), TSS ---
.section .data
.align 16
gdt_start:
    .quad 0x0000000000000000       # 0x00 null descriptor
    .quad 0x00AF9A000000FFFF       # 0x08 code64 ring0: present, exec/read, long-mode bit
    .quad 0x00AF92000000FFFF       # 0x10 data64 ring0: present, read/write
    .quad 0x00AFFA000000FFFF       # 0x18 code64 ring3: same as 0x08 but DPL=3 (access 0x9A->0xFA)
    .quad 0x00AFF2000000FFFF       # 0x20 data64 ring3: same as 0x10 but DPL=3 (access 0x92->0xF2)
    # 0x28 TSS descriptor - 16 bytes (2 slots) in long mode, since it needs
    # a full 64-bit base rather than the 32-bit one code/data segments get
    # away with under the flat memory model. The base-address fields
    # (marked 0 below) can't be filled in with static directives - `as`
    # can fold `tss_end - tss_start` (same section, resolves to a plain
    # constant) but not a bitwise/shift op directly on `tss_start` itself
    # (a symbol in a different, not-yet-relocated section) - so those get
    # patched with real instructions in _boot_start instead, before this
    # descriptor is ever loaded via `lgdt`.
    .word (tss_end - tss_start - 1) & 0xFFFF   # limit 0-15
    .word 0                                      # base 0-15 - patched at boot
    .byte 0                                      # base 16-23 - patched at boot
    .byte 0x89                                   # present, DPL0, type=1001 (64-bit TSS, available)
    .byte ((tss_end - tss_start - 1) >> 16) & 0xF   # limit 16-19 (flags nibble above it = 0)
    .byte 0                                      # base 24-31 - patched at boot
    .long 0                                      # base 32-63 (always 0 - kernel loads well under 4GB)
    .long 0                                      # reserved
gdt_end:

gdt_ptr:
    .word gdt_end - gdt_start - 1
    .long gdt_start   # `lgdt` here runs while the CPU is still 32-bit (the far
                       # jump below is what completes the switch to long mode),
                       # so the GDTR it reads is the 32-bit format: a 4-byte
                       # base, not the 10-byte 64-bit-mode one

gdt_ptr64:
    .word gdt_end - gdt_start - 1
    .long gdt_start   # base bits 0-31 - same 32-bit-relocation constraint as
    .long 0            # elsewhere in this file; base bits 32-63, always 0
                       # (kernel loads well under 4GB) - together these are
                       # exactly what a `.quad gdt_start` would have held,
                       # just split so each half only needs a representable
                       # 32-bit relocation. The second `lgdt` (in _start64,
                       # already 64-bit) reads this - full 10-byte GDTR.
