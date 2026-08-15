#!/usr/bin/env bash
# Builds the MiniC kernel: assembles boot.s, compiles kmain.mc through
# minicc (assembly only - a kernel's link step is too custom for minicc's
# own driver, so this script owns it), links with linker.ld.
#
# Usage: ./build.sh          # just build kernel.elf
#        ./build.sh run      # build, then boot it in QEMU (curses display)

set -e
cd "$(dirname "$0")"

# Kernel and compiler now live in separate repos (minic-os / minic) -
# defaults to the sibling-checkout layout (../compiler/build-linux/minicc),
# override with MINICC=/path/to/minicc ./build.sh for any other layout.
MINICC="${MINICC:-../compiler/build-linux/minicc}"

# Multiboot1 (and QEMU's/GRUB's loader for it) only understands a 32-bit
# ELF *container* - even though the code inside runs in 64-bit long mode
# once boot.s gets there. `.code32`/`.code64` are per-region encoding
# directives, independent of the container's ELF class, so both files
# assemble with `as --32`; minicc's output has no `.code64` directive of
# its own (it only ever targets hosted 64-bit ELF), so it's prepended here.
as --32 boot.s -o boot.o
as --32 interrupts.s -o interrupts.o
"$MINICC" kmain.mc -S --freestanding -o kmain.s
{ echo ".code64"; cat kmain.s; } | as --32 -o kmain.o
ld -m elf_i386 -T linker.ld -o kernel.elf boot.o interrupts.o kmain.o

echo "built kernel/kernel.elf"

if [ "$1" == "run" ]; then
    qemu-system-x86_64 -kernel kernel.elf -display curses
fi
