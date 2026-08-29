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

# Every generated .o/.gen.s/.d lands here, mirroring the source tree
# (build/drivers/io.o for drivers/io.c, etc), so `ls` in a source
# directory only ever shows the .c/.h that actually live there. The real
# deliverables (kernel.elf, minic-os.iso, disk.img) and the three ring3
# program .bin blobs stay where they are - the .bin files are read by
# `.incbin` in the hand-written *_blob.s files via a relative path (see
# the rule below), so moving them means also editing those .s files.
BUILD_DIR := build

CFLAGS := -ffreestanding -m64 -mgeneral-regs-only -mno-red-zone \
          -fno-stack-protector -fno-builtin -fPIC -fvisibility=hidden \
          -fcf-protection=none -Wall -Wextra -I.

# -MMD -MP emit a per-file .d listing every header a .c actually included,
# so a header-only change (e.g. adding a struct field) correctly triggers
# a rebuild of every .o that includes it - without this, `%.o: %.c`'s
# bare .c-only prerequisite left stale .o files silently linked next to
# freshly-recompiled ones sharing the SAME struct at a DIFFERENT size, a
# real bug that cost real debugging time to track down (looked exactly
# like a nondeterministic memory-corruption race, since which files were
# stale depended on exactly which .c files a given `make` happened to
# touch first).
# -MT $@ makes the generated rule's target the actual .o Make cares
# about, not the intermediate .gen.s gcc was told to write - otherwise
# the dependency is attached to a file nothing else's prerequisite list
# ever references, and doesn't actually trigger anything.
DEPFLAGS = -MMD -MP -MT $@ -MF $(basename $@).d

ASM_SRCS := boot/boot.s boot/interrupts.s sched/switch.s syscall/usermode.s
ASM_OBJS := $(addprefix $(BUILD_DIR)/,$(ASM_SRCS:.s=.o))

C_SRCS := $(patsubst ./%,%,$(shell find . -name '*.c' -not -path './proc/ring3prog.c' -not -path './proc/init.c' -not -path './proc/hello_service.c' -not -path './proc/desktop_shell.c' -not -path './proc/terminal.c' -not -path './proc/file_manager.c' -not -path './proc/settings.c' -not -path './.claude/*'))
C_OBJS := $(addprefix $(BUILD_DIR)/,$(C_SRCS:.c=.o))

.PHONY: all run iso disk clean

all: kernel.elf

# Hand-written assembly -> object, straight through. The static-pattern
# rule's target already carries the build/ prefix; Make recovers the
# matching %.s prerequisite from the bare stem.
$(ASM_OBJS): $(BUILD_DIR)/%.o: %.s
	@mkdir -p $(dir $@)
	$(AS) --32 $< -o $@

# C -> a *generated* .gen.s (distinct suffix so it never collides with a
# real hand-written .s file's own name, which would otherwise make a
# stale generated file win over recompiling from source on a later
# `make`) -> object, via the .code64-prepend trick (see header comment).
$(C_OBJS): $(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEPFLAGS) -S -o $(BUILD_DIR)/$(<:.c=.gen.s) $<
	{ echo ".code64"; cat $(BUILD_DIR)/$(<:.c=.gen.s); } | $(AS) --32 -o $@

-include $(C_OBJS:.o=.d)

# The loaded ring3 "program" is real compiled C, not hand-assembled -
# but spawn_process() (proc/process.c) still just copies one contiguous
# byte range and jumps to its first byte, so the compiled program needs
# its own SEPARATE standalone link (proc/ring3.ld keeps .text/.rodata/
# .data/.bss contiguous, with nothing else's sections in between)
# before it can be objcopy'd into one flat blob and wrapped with the
# g_test_prog_start/g_test_prog_end marker symbols everything downstream
# expects (proc/ring3blob.s). Linking the compiled object straight into
# kernel.elf the way every other .o is would NOT work: `ld` groups every
# input object's .text together, then every .rodata, etc, so this
# program's code and string literals would land far apart in the final
# image, breaking the "one contiguous copyable blob" assumption the
# whole loader depends on. Every intermediate here (.gen.s, _raw.o,
# _linked.elf, .d) goes into build/proc/ - only the final .bin, read by
# .incbin below, stays in proc/ itself.
proc/ring3prog.bin: proc/ring3prog.c proc/ring3.ld
	@mkdir -p $(BUILD_DIR)/proc
	$(CC) $(CFLAGS) -MMD -MP -MT proc/ring3prog.bin -MF $(BUILD_DIR)/proc/ring3prog.d -S -o $(BUILD_DIR)/proc/ring3prog.gen.s proc/ring3prog.c
	{ echo ".code64"; cat $(BUILD_DIR)/proc/ring3prog.gen.s; } | $(AS) --32 -o $(BUILD_DIR)/proc/ring3prog_raw.o
	$(LD) -m elf_i386 -T proc/ring3.ld -o $(BUILD_DIR)/proc/ring3prog_linked.elf $(BUILD_DIR)/proc/ring3prog_raw.o
	$(OBJCOPY) -O binary --set-section-flags .bss=alloc,load,contents \
		$(BUILD_DIR)/proc/ring3prog_linked.elf proc/ring3prog.bin

-include $(BUILD_DIR)/proc/ring3prog.d

# Two more standalone-linked ring3 programs (init, and the trivial
# service it spawns) - same shape as ring3prog.bin above, just two more.
proc/init.bin: proc/init.c proc/ring3.ld
	@mkdir -p $(BUILD_DIR)/proc
	$(CC) $(CFLAGS) -MMD -MP -MT proc/init.bin -MF $(BUILD_DIR)/proc/init.d -S -o $(BUILD_DIR)/proc/init.gen.s proc/init.c
	{ echo ".code64"; cat $(BUILD_DIR)/proc/init.gen.s; } | $(AS) --32 -o $(BUILD_DIR)/proc/init_raw.o
	$(LD) -m elf_i386 -T proc/ring3.ld -o $(BUILD_DIR)/proc/init_linked.elf $(BUILD_DIR)/proc/init_raw.o
	$(OBJCOPY) -O binary --set-section-flags .bss=alloc,load,contents \
		$(BUILD_DIR)/proc/init_linked.elf proc/init.bin

-include $(BUILD_DIR)/proc/init.d

proc/hello_service.bin: proc/hello_service.c proc/ring3.ld
	@mkdir -p $(BUILD_DIR)/proc
	$(CC) $(CFLAGS) -MMD -MP -MT proc/hello_service.bin -MF $(BUILD_DIR)/proc/hello_service.d -S -o $(BUILD_DIR)/proc/hello_service.gen.s proc/hello_service.c
	{ echo ".code64"; cat $(BUILD_DIR)/proc/hello_service.gen.s; } | $(AS) --32 -o $(BUILD_DIR)/proc/hello_service_raw.o
	$(LD) -m elf_i386 -T proc/ring3.ld -o $(BUILD_DIR)/proc/hello_service_linked.elf $(BUILD_DIR)/proc/hello_service_raw.o
	$(OBJCOPY) -O binary --set-section-flags .bss=alloc,load,contents \
		$(BUILD_DIR)/proc/hello_service_linked.elf proc/hello_service.bin

-include $(BUILD_DIR)/proc/hello_service.d

# The desktop shell - same shape again, auto-spawned by kmain.c alongside
# init (not shell-triggered like ring3prog.c's demos).
proc/desktop_shell.bin: proc/desktop_shell.c proc/gui_toolkit.h proc/ring3.ld
	@mkdir -p $(BUILD_DIR)/proc
	$(CC) $(CFLAGS) -MMD -MP -MT proc/desktop_shell.bin -MF $(BUILD_DIR)/proc/desktop_shell.d -S -o $(BUILD_DIR)/proc/desktop_shell.gen.s proc/desktop_shell.c
	{ echo ".code64"; cat $(BUILD_DIR)/proc/desktop_shell.gen.s; } | $(AS) --32 -o $(BUILD_DIR)/proc/desktop_shell_raw.o
	$(LD) -m elf_i386 -T proc/ring3.ld -o $(BUILD_DIR)/proc/desktop_shell_linked.elf $(BUILD_DIR)/proc/desktop_shell_raw.o
	$(OBJCOPY) -O binary --set-section-flags .bss=alloc,load,contents \
		$(BUILD_DIR)/proc/desktop_shell_linked.elf proc/desktop_shell.bin

-include $(BUILD_DIR)/proc/desktop_shell.d

# The terminal emulator - same shape again, auto-spawned by kmain.c
# alongside desktop_shell/init.
proc/terminal.bin: proc/terminal.c proc/gui_toolkit.h proc/ring3.ld
	@mkdir -p $(BUILD_DIR)/proc
	$(CC) $(CFLAGS) -MMD -MP -MT proc/terminal.bin -MF $(BUILD_DIR)/proc/terminal.d -S -o $(BUILD_DIR)/proc/terminal.gen.s proc/terminal.c
	{ echo ".code64"; cat $(BUILD_DIR)/proc/terminal.gen.s; } | $(AS) --32 -o $(BUILD_DIR)/proc/terminal_raw.o
	$(LD) -m elf_i386 -T proc/ring3.ld -o $(BUILD_DIR)/proc/terminal_linked.elf $(BUILD_DIR)/proc/terminal_raw.o
	$(OBJCOPY) -O binary --set-section-flags .bss=alloc,load,contents \
		$(BUILD_DIR)/proc/terminal_linked.elf proc/terminal.bin

-include $(BUILD_DIR)/proc/terminal.d

# The file manager - same shape again, auto-spawned by kmain.c alongside
# desktop_shell/terminal/init.
proc/file_manager.bin: proc/file_manager.c proc/gui_toolkit.h proc/ring3.ld disk/minifs.h
	@mkdir -p $(BUILD_DIR)/proc
	$(CC) $(CFLAGS) -MMD -MP -MT proc/file_manager.bin -MF $(BUILD_DIR)/proc/file_manager.d -S -o $(BUILD_DIR)/proc/file_manager.gen.s proc/file_manager.c
	{ echo ".code64"; cat $(BUILD_DIR)/proc/file_manager.gen.s; } | $(AS) --32 -o $(BUILD_DIR)/proc/file_manager_raw.o
	$(LD) -m elf_i386 -T proc/ring3.ld -o $(BUILD_DIR)/proc/file_manager_linked.elf $(BUILD_DIR)/proc/file_manager_raw.o
	$(OBJCOPY) -O binary --set-section-flags .bss=alloc,load,contents \
		$(BUILD_DIR)/proc/file_manager_linked.elf proc/file_manager.bin

-include $(BUILD_DIR)/proc/file_manager.d

# System Settings - same shape again, auto-spawned by kmain.c alongside
# desktop_shell/terminal/file_manager/init.
proc/settings.bin: proc/settings.c proc/gui_toolkit.h proc/ring3.ld
	@mkdir -p $(BUILD_DIR)/proc
	$(CC) $(CFLAGS) -MMD -MP -MT proc/settings.bin -MF $(BUILD_DIR)/proc/settings.d -S -o $(BUILD_DIR)/proc/settings.gen.s proc/settings.c
	{ echo ".code64"; cat $(BUILD_DIR)/proc/settings.gen.s; } | $(AS) --32 -o $(BUILD_DIR)/proc/settings_raw.o
	$(LD) -m elf_i386 -T proc/ring3.ld -o $(BUILD_DIR)/proc/settings_linked.elf $(BUILD_DIR)/proc/settings_raw.o
	$(OBJCOPY) -O binary --set-section-flags .bss=alloc,load,contents \
		$(BUILD_DIR)/proc/settings_linked.elf proc/settings.bin

-include $(BUILD_DIR)/proc/settings.d

# `.incbin` in each *_blob.s resolves relative to the assembler's own
# working directory, not the .s file's location - `cd proc` first,
# matching the MiniC-era build's own convention. `../$@` still lands the
# output back in build/proc/ since $@ already carries that full prefix.
$(BUILD_DIR)/proc/ring3blob.o: proc/ring3blob.s proc/ring3prog.bin
	@mkdir -p $(BUILD_DIR)/proc
	cd proc && $(AS) --32 ring3blob.s -o ../$@

$(BUILD_DIR)/proc/init_blob.o: proc/init_blob.s proc/init.bin
	@mkdir -p $(BUILD_DIR)/proc
	cd proc && $(AS) --32 init_blob.s -o ../$@

$(BUILD_DIR)/proc/hello_service_blob.o: proc/hello_service_blob.s proc/hello_service.bin
	@mkdir -p $(BUILD_DIR)/proc
	cd proc && $(AS) --32 hello_service_blob.s -o ../$@

$(BUILD_DIR)/proc/desktop_shell_blob.o: proc/desktop_shell_blob.s proc/desktop_shell.bin
	@mkdir -p $(BUILD_DIR)/proc
	cd proc && $(AS) --32 desktop_shell_blob.s -o ../$@

$(BUILD_DIR)/proc/terminal_blob.o: proc/terminal_blob.s proc/terminal.bin
	@mkdir -p $(BUILD_DIR)/proc
	cd proc && $(AS) --32 terminal_blob.s -o ../$@

$(BUILD_DIR)/proc/file_manager_blob.o: proc/file_manager_blob.s proc/file_manager.bin
	@mkdir -p $(BUILD_DIR)/proc
	cd proc && $(AS) --32 file_manager_blob.s -o ../$@

$(BUILD_DIR)/proc/settings_blob.o: proc/settings_blob.s proc/settings.bin
	@mkdir -p $(BUILD_DIR)/proc
	cd proc && $(AS) --32 settings_blob.s -o ../$@

kernel.elf: $(ASM_OBJS) $(C_OBJS) $(BUILD_DIR)/proc/ring3blob.o $(BUILD_DIR)/proc/init_blob.o $(BUILD_DIR)/proc/hello_service_blob.o $(BUILD_DIR)/proc/desktop_shell_blob.o $(BUILD_DIR)/proc/terminal_blob.o $(BUILD_DIR)/proc/file_manager_blob.o $(BUILD_DIR)/proc/settings_blob.o
	$(LD) -m elf_i386 -T boot/linker.ld -o $@ $(ASM_OBJS) $(C_OBJS) $(BUILD_DIR)/proc/ring3blob.o $(BUILD_DIR)/proc/init_blob.o $(BUILD_DIR)/proc/hello_service_blob.o $(BUILD_DIR)/proc/desktop_shell_blob.o $(BUILD_DIR)/proc/terminal_blob.o $(BUILD_DIR)/proc/file_manager_blob.o $(BUILD_DIR)/proc/settings_blob.o
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
	rm -rf $(BUILD_DIR)
	rm -f kernel.elf minic-os.iso disk.img
	rm -f proc/ring3prog.bin proc/ring3prog_linked.elf
	rm -f proc/init.bin proc/init_linked.elf
	rm -f proc/hello_service.bin proc/hello_service_linked.elf
	rm -f proc/desktop_shell.bin proc/desktop_shell_linked.elf
	rm -f proc/terminal.bin proc/terminal_linked.elf
	rm -f proc/file_manager.bin proc/file_manager_linked.elf
	rm -f proc/settings.bin proc/settings_linked.elf
