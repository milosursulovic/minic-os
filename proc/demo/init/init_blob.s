# Wraps the flattened init.bin (Makefile's standalone link+objcopy
# sub-pipeline, same shape as ring3blob.s for ring3prog.bin) with its
# own marker symbol names - kmain.c spawns init from these directly, at
# boot, the same mechanism it already uses for the existing demo blob.
.intel_syntax noprefix

.global g_init_prog_start
.global g_init_prog_end

g_init_prog_start:
.incbin "../../../build/proc/demo/init/init.bin"
g_init_prog_end:
