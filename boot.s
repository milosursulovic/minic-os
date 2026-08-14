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

# ---- 32-bit entry point ---------------------------------------------------
.section .text
.code32
.global _boot_start
.extern _start              # MiniC's void _start(), in kmain.mc

_boot_start:
    mov esp, offset stack_top

    # Identity-map the first 1GB with 2MB pages: PML4[0] -> pdpt,
    # PDPT[0] -> pd, PD[0..511] each a 2MB page (PS bit set, no PT level
    # needed). Covers the kernel itself (loaded at 1MB) and the VGA text
    # buffer at 0xB8000 alike.
    mov eax, offset pdpt
    or eax, 0x03                # present + writable
    mov [pml4], eax
    mov dword ptr [pml4 + 4], 0

    mov eax, offset pd
    or eax, 0x03
    mov [pdpt], eax
    mov dword ptr [pdpt + 4], 0

    mov ecx, 0
.fill_pd:
    mov eax, ecx
    shl eax, 21                 # eax = ecx * 2MB
    or eax, 0x83                # present + writable + page-size (2MB)
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

    # Set the Long Mode Enable bit in EFER (MSR 0xC0000080).
    mov ecx, 0xC0000080
    rdmsr
    or eax, 0x100
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
    call _start

.hang:
    hlt
    jmp .hang

# ---- Minimal flat GDT: null, 64-bit code, 64-bit data ---------------------
.section .data
.align 16
gdt_start:
    .quad 0x0000000000000000       # null descriptor
    .quad 0x00AF9A000000FFFF       # code64: present, ring0, exec/read, long-mode bit
    .quad 0x00AF92000000FFFF       # data64: present, ring0, read/write
gdt_end:

gdt_ptr:
    .word gdt_end - gdt_start - 1
    .long gdt_start   # `lgdt` here runs while the CPU is still 32-bit (the far
                       # jump below is what completes the switch to long mode),
                       # so the GDTR it reads is the 32-bit format: a 4-byte
                       # base, not the 10-byte 64-bit-mode one
