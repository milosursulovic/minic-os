# Wraps the flattened device_manager.bin (Makefile's standalone
# link+objcopy sub-pipeline, same shape as settings_blob.s) with its own
# marker symbol names - desktop_shell.c's MENU dropdown spawns this on
# demand via syscall 41 (gui_app_bounds() app_id 3), not at boot.
.intel_syntax noprefix

.global g_device_manager_prog_start
.global g_device_manager_prog_end

g_device_manager_prog_start:
.incbin "../../../build/proc/apps/device_manager/device_manager.bin"
g_device_manager_prog_end:
