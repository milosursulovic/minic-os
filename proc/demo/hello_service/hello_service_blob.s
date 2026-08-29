# Wraps the flattened hello_service.bin the same way ring3blob.s/
# init_blob.s wrap their own programs - these marker symbols are what
# syscall.c's g_builtin_programs table (see syscall.c's new syscall 11,
# spawn_builtin) references, the kernel-controlled registry a ring3
# process can spawn from by INDEX rather than by an arbitrary raw
# pointer it could otherwise forge.
.intel_syntax noprefix

.global g_hello_service_prog_start
.global g_hello_service_prog_end

g_hello_service_prog_start:
.incbin "../../../build/proc/demo/hello_service/hello_service.bin"
g_hello_service_prog_end:
