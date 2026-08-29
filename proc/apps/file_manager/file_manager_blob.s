# Wraps the flattened file_manager.bin (Makefile's standalone link+objcopy
# sub-pipeline, same shape as desktop_shell_blob.s/terminal_blob.s) with its
# own marker symbol names - kmain.c spawns the file manager from these
# directly, at boot, the same mechanism it already uses for init/the
# desktop shell/the terminal.
.intel_syntax noprefix

.global g_file_manager_prog_start
.global g_file_manager_prog_end

g_file_manager_prog_start:
.incbin "../../../build/proc/apps/file_manager/file_manager.bin"
g_file_manager_prog_end:
