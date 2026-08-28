# Wraps the flattened terminal.bin (Makefile's standalone link+objcopy
# sub-pipeline, same shape as desktop_shell_blob.s/init_blob.s) with its
# own marker symbol names - kmain.c spawns the terminal from these
# directly, at boot, the same mechanism it already uses for init/the
# desktop shell.
.intel_syntax noprefix

.global g_terminal_prog_start
.global g_terminal_prog_end

g_terminal_prog_start:
.incbin "terminal.bin"
g_terminal_prog_end:
