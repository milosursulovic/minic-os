# Wraps the flattened wifi.bin (Makefile's standalone link+objcopy
# sub-pipeline, same shape as settings_blob.s/device_manager_blob.s)
# with its own marker symbol names - kernel/syscall/handlers/system.c's
# gui_app_bounds() (app_id 5) spawns this on demand, same mechanism as
# every other menu-launched GUI app.
.intel_syntax noprefix

.global g_wifi_prog_start
.global g_wifi_prog_end

g_wifi_prog_start:
.incbin "../../../build/proc/apps/wifi/wifi.bin"
g_wifi_prog_end:
