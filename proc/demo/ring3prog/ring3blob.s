# Wraps the flattened ring3prog.bin (produced by build.sh's standalone
# link+objcopy sub-pipeline - see proc/ring3.ld) with the same marker
# symbols proc/process.mc, kmain.mc, and shell.mc have always used for
# the loaded test program, so nothing downstream of this file needs to
# change: only *how* the bytes between these two labels get produced is
# new, not their name or meaning.
.intel_syntax noprefix

.global g_test_prog_start
.global g_test_prog_end

g_test_prog_start:
.incbin "../../../build/proc/demo/ring3prog/ring3prog.bin"
g_test_prog_end:
