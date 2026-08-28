# Wraps the flattened desktop_shell.bin (Makefile's standalone link+
# objcopy sub-pipeline, same shape as init_blob.s/ring3blob.s) with its
# own marker symbol names - kmain.c spawns the desktop shell from these
# directly, at boot, the same mechanism it already uses for init/the
# ring3 test program.
.intel_syntax noprefix

.global g_desktop_shell_prog_start
.global g_desktop_shell_prog_end

g_desktop_shell_prog_start:
.incbin "desktop_shell.bin"
g_desktop_shell_prog_end:
