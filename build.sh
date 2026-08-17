#!/usr/bin/env bash
# Builds the MiniC kernel: assembles boot.s, compiles kmain.mc through
# minicc (assembly only - a kernel's link step is too custom for minicc's
# own driver, so this script owns it), links with linker.ld.
#
# Usage: ./build.sh          # just build kernel.elf
#        ./build.sh run      # build, then boot it in QEMU (curses display)
#        ./build.sh iso      # build, then package a GRUB-bootable minic-os.iso
#                             # (for VirtualBox/VMware/real hardware - QEMU's
#                             # -kernel is a QEMU-only shortcut, everything
#                             # else needs a real bootloader)

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
as --32 boot/boot.s -o boot.o
as --32 boot/interrupts.s -o interrupts.o
as --32 sched/switch.s -o switch.o
as --32 syscall/usermode.s -o usermode.o

# Milestone 21: the loaded ring3 "program" is real compiled MiniC now,
# not hand-assembled - but spawnProcess() (proc/process.mc) still just
# copies one contiguous byte range and jumps to its first byte, so the
# compiled program needs its own SEPARATE standalone link (proc/ring3.ld
# keeps .text/.rodata/.data/.bss contiguous, with nothing else's sections
# in between) before it can be objcopy'd into one flat blob and wrapped
# with the same gTestProgStart/gTestProgEnd markers everything downstream
# already expects (proc/ring3blob.s). Linking the compiled object
# straight into kernel.elf the way testprog.o used to be would NOT work:
# `ld` groups every input object's .text together, then every .rodata,
# etc, so this program's code and string literals would land far apart
# in the final image, breaking that same contiguous-byte-range assumption.
"$MINICC" proc/ring3prog.mc -S --freestanding -o proc/ring3prog.s
{ echo ".code64"; cat proc/ring3prog.s; } | as --32 -o proc/ring3prog_raw.o
ld -m elf_i386 -T proc/ring3.ld -o proc/ring3prog_linked.elf proc/ring3prog_raw.o
# Milestone 24 found a real, latent bug in this pipeline: `objcopy -O
# binary` drops a *trailing* NOBITS section (.bss) entirely rather than
# padding it with zeros, since a raw binary format has no way to
# represent "this is zero-filled" - it can only include bytes it
# actually has. Every milestone before this one got away with it purely
# by luck (globals were always written before ever being read, so
# whatever garbage sat in that unbacked memory never actually showed
# up) - milestone 24's open() is the first code here that reads a
# global (gFdTable[i].used) before writing it, which exposed real
# garbage there and produced a GPF. --set-section-flags forces objcopy
# to treat .bss as real, zero-filled content instead of skipping it,
# so the flattened blob's on-disk size (and therefore how many bytes/
# pages spawnProcess() copies and maps) actually covers the program's
# real memory footprint.
objcopy -O binary --set-section-flags .bss=alloc,load,contents proc/ring3prog_linked.elf proc/ring3prog.bin
(cd proc && as --32 ring3blob.s -o ../testprog.o)

"$MINICC" kmain.mc -S --freestanding -o kmain.s
{ echo ".code64"; cat kmain.s; } | as --32 -o kmain.o
ld -m elf_i386 -T boot/linker.ld -o kernel.elf boot.o interrupts.o switch.o usermode.o testprog.o kmain.o

echo "built kernel.elf"

# A small raw disk image for milestone 16's ATA PIO driver to talk to -
# not a real filesystem yet (that's milestone 17+), just known bytes at
# known LBAs so the driver's read/write path has something real to prove
# itself against. LBA 1 gets a fixed ASCII signature (the shell's `disk`
# command reads it back); LBA 100 is left zeroed, exercised by
# `diskwrite`. 2048 sectors = 1MB, plenty for now. Regenerated from
# scratch every time - it's a known-bytes test fixture, not real data.
ensure_disk() {
    dd if=/dev/zero of=disk.img bs=512 count=2048 status=none
    printf 'MiniC ATA PIO driver - milestone 16 signature sector' | \
        dd of=disk.img bs=512 seek=1 conv=notrunc status=none
}

if [ "$1" == "disk" ]; then
    ensure_disk
    echo "built disk.img"
fi

if [ "$1" == "run" ]; then
    [ -f disk.img ] || ensure_disk
    qemu-system-x86_64 -kernel kernel.elf -display curses \
        -drive file=disk.img,format=raw,if=ide
fi

if [ "$1" == "iso" ]; then
    # grub-mkrescue wants the kernel inside the ISO tree it packages -
    # iso/boot/grub/grub.cfg is checked in, iso/boot/kernel.elf is just
    # today's build copied into place.
    cp kernel.elf iso/boot/kernel.elf
    grub-mkrescue -o minic-os.iso iso
    echo "built minic-os.iso"
fi
