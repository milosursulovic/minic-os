# Builds the C kernel: assembles the hand-written .s files (boot/interrupt
# entry/context switch/ring3 entry - genuinely below what inline asm can
# express, see CLAUDE.md), compiles every .c through gcc, links into a
# multiboot1 kernel.elf.
#
# QEMU's `-kernel` multiboot1 loader hard-rejects a 64-bit ELF outright
# ("Cannot load x86-64 image, give a 32bit one.") even though the code
# inside runs in 64-bit long mode - so every object still lands in a
# 32-bit ELF *container* (`as --32` for the hand-written .s files, and for
# the C files: compile to assembly with real x86-64 codegen, prepend
# `.code64`, then assemble that with `as --32` too), exactly like the
# kernel's MiniC-era build already did. Two real wrinkles C introduces
# that MiniC's own simplistic codegen never hit:
#  1. Without `-fPIC`, gcc happily materializes a string/array's absolute
#     address as a 64-bit sign-extended immediate (`movq $label, mem`),
#     needing relocation type R_X86_64_32S - not representable in an
#     ELF32 relocation table, so `as` rejects it outright. `-fPIC` makes
#     gcc use RIP-relative `lea` instead (the same convention MiniC's own
#     codegen always used, for exactly this reason), which assembles fine.
#  2. `-fPIC` alone still routes references to any externally-linkable
#     global (not just local statics) through the GOT (`mov
#     sym@GOTPCREL(%rip), reg`) in case another shared object interposes
#     it - `@GOTPCREL` is an ELF64-only relocation syntax `as --32` can't
#     even parse. `-fvisibility=hidden` tells gcc no symbol here will ever
#     be interposed (true - this is one statically-linked kernel image,
#     never dynamically linked), so it uses a direct RIP-relative
#     reference instead of a GOT indirection.

CC := gcc
AS := as
LD := ld
OBJCOPY := objcopy

CFLAGS := -ffreestanding -m64 -mgeneral-regs-only -mno-red-zone \
          -fno-stack-protector -fno-builtin -fPIC -fvisibility=hidden \
          -fcf-protection=none -Wall -Wextra -I.

ASM_SRCS := boot/boot.s boot/interrupts.s sched/switch.s syscall/usermode.s
ASM_OBJS := $(ASM_SRCS:.s=.o)

C_SRCS := $(shell find . -name '*.c' -not -path './proc/ring3prog.c')
C_OBJS := $(C_SRCS:.c=.o)

.PHONY: all run iso disk clean

all: kernel.elf

# Hand-written assembly -> object, straight through.
$(ASM_OBJS): %.o: %.s
	$(AS) --32 $< -o $@

# C -> a *generated* .gen.s (distinct suffix so it never collides with a
# real hand-written .s file's own name, which would otherwise make a
# stale generated file win over recompiling from source on a later
# `make`) -> object, via the .code64-prepend trick (see header comment).
$(C_OBJS): %.o: %.c
	$(CC) $(CFLAGS) -S -o $(<:.c=.gen.s) $<
	{ echo ".code64"; cat $(<:.c=.gen.s); } | $(AS) --32 -o $@

kernel.elf: $(ASM_OBJS) $(C_OBJS)
	$(LD) -m elf_i386 -T boot/linker.ld -o $@ $(ASM_OBJS) $(C_OBJS)
	@echo "built kernel.elf"

disk.img:
	dd if=/dev/zero of=disk.img bs=512 count=2048 status=none
	printf 'ATA PIO driver test signature sector' | \
		dd of=disk.img bs=512 seek=1 conv=notrunc status=none

.PHONY: disk
disk:
	rm -f disk.img
	$(MAKE) disk.img
	@echo "built disk.img"

run: kernel.elf disk.img
	qemu-system-x86_64 -kernel kernel.elf -display curses \
		-drive file=disk.img,format=raw,if=ide

iso: kernel.elf
	cp kernel.elf iso/boot/kernel.elf
	grub-mkrescue -o minic-os.iso iso
	@echo "built minic-os.iso"

clean:
	find . -name '*.o' -delete
	find . -name '*.gen.s' -delete
	rm -f kernel.elf minic-os.iso disk.img
