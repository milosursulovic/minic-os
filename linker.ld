/* Multiboot loads a kernel at 1MB by convention. The multiboot header has
 * to be within the first 8KB of the file, so .multiboot goes first. */
ENTRY(_boot_start)

SECTIONS
{
    . = 1M;

    /* .multiboot must be first *within* .text, not its own output section -
     * two separate output sections here left the location counter at 1M
     * for both (an ld quirk), silently overlapping the multiboot header
     * with the real code it's supposed to precede. */
    .text ALIGN(4K) : {
        *(.multiboot)
        *(.text)
    }

    .rodata ALIGN(4K) : {
        *(.rodata)
    }

    .data ALIGN(4K) : {
        *(.data)
    }

    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss)
    }
}
