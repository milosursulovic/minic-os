# Wraps the flattened settings.bin (Makefile's standalone link+objcopy
# sub-pipeline, same shape as desktop_shell_blob.s/file_manager_blob.s)
# with its own marker symbol names - kmain.c spawns Settings from these
# directly, at boot, the same mechanism it already uses for the desktop
# shell/terminal/file manager.
.intel_syntax noprefix

.global g_settings_prog_start
.global g_settings_prog_end

g_settings_prog_start:
.incbin "settings.bin"
g_settings_prog_end:
