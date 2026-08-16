/* Standalone link for a single compiled-MiniC ring3 program (milestone
 * 21) - deliberately NOT linked directly into kernel.elf alongside
 * everything else, unlike every other .o this kernel builds. The point
 * of this separate link is to force .text/.rodata/.data/.bss to sit
 * contiguously with no other object's sections in between, so the
 * result can be objcopy'd into one flat, truly contiguous blob that
 * spawnProcess()'s "copy one byte range, jump to its first byte" loader
 * can still handle unmodified - see build.sh and proc/ring3blob.s. */
ENTRY(_start)

SECTIONS
{
    . = 0;

    .text : { *(.text) }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : { *(COMMON) *(.bss) }
}
