// The first real ring0/ring3 boundary. `int 0x80` (DPL=3, wired up in
// drivers/interrupts_init.c) is the gate; boot/interrupts.s's isr_syscall
// stub reads the caller's syscall number/args off the stack and calls
// syscall_dispatch() below - same relationship isr.c's interrupt_handler
// has to the hardware interrupt stubs, just with real arguments and a
// real return value instead of just a vector number.
//
// Calling convention (ours to define, since this goes through a software
// `int n` gate rather than the SYSCALL/SYSRET instruction pair with its
// own fixed convention): rax = syscall number in, return value out;
// rdi/rsi/rdx = up to three arguments.
//
// num 1: print arg1 (a char* the caller owns) followed by arg2 in hex.
// num 3: resolve arg1 as a handle *within the calling process's own
// handle table* and return a piece of ground-truth info about whatever
// it points to - for a process object, its task_index, independently
// checkable against the `ps` shell command's own output. A handle
// that's out of range, never allocated, or belongs to a plain kernel
// task with no handle table at all, or lacks RIGHT_QUERY, all return
// the same -1 sentinel rather than trusting the index or crashing.
// num 4/5: vfs_read/vfs_write - a real slice of the native "File" API's
// job. arg1/arg2 are a caller-owned char*/buffer pointer pair; since a
// syscall runs with the caller's own CR3 still loaded (no address-space
// switch happens on entry), these are safe to dereference directly.
// Neither direction validates that arg1/arg2 point at memory the
// calling process actually owns (no real memory-safety enforcement yet
// - a known, deliberate limitation).
// num 6: spawn - wraps Process the same way num 4/5 wrapped File.
// Reuses spawn_process_from_path() completely unchanged (the exact same
// function the shell's `spawn` command already calls in kernel mode -
// only the caller is new, not the mechanism) and returns the new
// process's task_index, same convention syscall 3 already established.
// num 7/8/9: channel_send/channel_receive/open_channel. channel_receive
// is the first BLOCKING syscall this kernel has - it calls
// yield()/switch_context() same as it always has when called from a
// kernel task directly, and since syscall_dispatch runs as an ordinary
// nested call within the *calling* ring3 task's own context, blocking
// here suspends the right task and correctly resumes through
// isr_syscall's iretq once woken. arg1 is a real HANDLE, resolved
// through the calling process's own handle table with real per-handle
// RIGHTS checked before the underlying channel is touched at all - the
// actual "capability" in "capability/permission system". num 9
// (open_channel) is the one place a ring3 process can turn a channel
// index into a handle - and it's also the one place rights POLICY is
// decided: it always grants RIGHT_RECEIVE only, never RIGHT_SEND. A
// handle's rights are fixed forever at grant time - there's no way to
// widen one later, only to open a new one under whatever policy the
// kernel chooses at that call site.
// num 10: open_process - the first real cross-process capability: given
// another task's index (not necessarily the caller's own), it mints a
// handle to THAT process in the caller's own table - the caller
// REQUESTS a rights bitmask (arg2), and the kernel grants the
// intersection of what was requested and what's actually grantable for
// an OBJ_PROCESS handle today (`requested & RIGHT_QUERY`).
// num 12: process_exit (milestone 37) - the calling process's own task
// slot and process slot both get marked no-longer-alive, then yield()
// switches away for the last time; the scheduler's scan (sched/task.c)
// permanently skips a slot marked this way, the same way it already
// skips a blocked one, just with no wake condition that will ever fire.
// Deliberately does NOT reclaim the exited process's resources yet
// (its frames stay mapped, its process/task table slot stays occupied,
// any handles pointing at it stay valid but now point at a process that
// will never run again) - real, separate follow-on work, not an
// oversight; see README's Known Limitations.
// num 11: spawn_builtin (milestone 36) - lets a ring3 process spawn
// ANOTHER ring3 process from a small, fixed, kernel-embedded program
// registry (g_builtin_programs below), addressed by INDEX rather than
// a raw pointer - the real reason this is its own syscall instead of
// just exposing spawn_process() directly to ring3: a raw-pointer
// version would let ring3 code claim any address in memory is a valid
// program image, exactly the kind of hole the capability/rights phase
// (milestones 25-27) worked to close everywhere else. num 6
// (spawn, via a VFS path) already covers "spawn a real file from disk";
// this covers "spawn one of a few programs the kernel itself shipped
// with," the mechanism proc/init.c uses to launch proc/hello_service.c
// without needing the filesystem to have anything on it yet.

#include "syscall.h"
#include "../drivers/io.h"
#include "../lib/strings.h"
#include "../sched/task.h"
#include "../proc/process.h"
#include "../proc/object.h"
#include "../proc/channel.h"
#include "../disk/vfs.h"

#pragma GCC visibility push(hidden)
extern u8 g_hello_service_prog_start;
extern u8 g_hello_service_prog_end;
#pragma GCC visibility pop

// Deliberately NOT a static const array of {start, end} pointers
// initialized with &symbol expressions - that needs an absolute 64-bit
// relocation (R_X86_64_64) to bake the real addresses into static data,
// which isn't representable in the ELF32 container `as --32` produces
// (same class of restriction CLAUDE.md documents for hand-written asm:
// no `.quad <label>` as static data). A plain if/else resolving each
// address at the point of use - the same RIP-relative `lea` every other
// `&marker_symbol` reference in this kernel already compiles to as an
// ordinary runtime expression - sidesteps it entirely, and is honestly
// simpler for the one entry this registry has today anyway.
static bool builtin_program_bounds(int index, u8** start_out, u8** end_out) {
    if (index == 0) {
        *start_out = &g_hello_service_prog_start;
        *end_out = &g_hello_service_prog_end;
        return true;
    }
    return false;
}

// Same fixed load/stack addresses spawn_process() uses everywhere else
// in this kernel - safe to reuse across every concurrently-running
// process since each gets its own private, cloned address space
// (clone_address_space()), already proven by the boot-time demo's own
// Process.spawn() call landing a second instance at these identical
// virtual addresses successfully.
static const u64 BUILTIN_LOAD_VADDR = 0x80000000;
static const u64 BUILTIN_STACK_VADDR = 0x80020000;

u64 syscall_dispatch(u64 num, u64 a1, u64 a2, u64 a3) {
    if (num == 1) {
        char* s = (char*) a1;
        serial_print(s);
        vga_print(s);
        print_hex(a2);
        serial_print("\n");
        return 0;
    }
    if (num == 3) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            return (u64) -1;
        }
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_QUERY) == 0) {
            return (u64) -1;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type == OBJ_PROCESS) {
            int proc_idx = g_objects[obj_index].data_index;
            return (u64) g_processes[proc_idx].task_index;
        }
        return (u64) -1;
    }
    if (num == 4) {
        char* path = (char*) a1;
        u8* buf = (u8*) a2;
        int n = vfs_read(path, buf, (u32) a3);
        if (n < 0) {
            return (u64) -1;
        }
        return (u64) n;
    }
    if (num == 5) {
        char* path = (char*) a1;
        u8* buf = (u8*) a2;
        bool ok = vfs_write(path, buf, (u32) a3);
        if (!ok) {
            return (u64) -1;
        }
        return a3;
    }
    if (num == 6) {
        char* path = (char*) a1;
        int proc_index = spawn_process_from_path(path, a2, a3);
        if (proc_index < 0) {
            return (u64) -1;
        }
        return (u64) g_processes[proc_index].task_index;
    }
    if (num == 7) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            return (u64) -1;
        }
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_SEND) == 0) {
            return (u64) -1;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_CHANNEL) {
            return (u64) -1;
        }
        int channel_index = g_objects[obj_index].data_index;
        bool ok = channel_send(channel_index, a2);
        if (!ok) {
            return (u64) -1;
        }
        return 0;
    }
    if (num == 8) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            return (u64) -1;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            return (u64) -1;
        }
        if ((g_handle_tables[caller_process][handle_idx].rights & RIGHT_RECEIVE) == 0) {
            return (u64) -1;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_CHANNEL) {
            return (u64) -1;
        }
        int channel_index = g_objects[obj_index].data_index;
        return channel_receive(channel_index);
    }
    if (num == 9) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int channel_index = (int) a1;
        if (channel_index < 0 || channel_index >= g_channel_count) {
            return (u64) -1;
        }
        int obj_index = alloc_object(OBJ_CHANNEL, channel_index);
        if (obj_index < 0) {
            return (u64) -1;
        }
        int handle_idx = alloc_handle(caller_process, obj_index, RIGHT_RECEIVE);
        if (handle_idx < 0) {
            return (u64) -1;
        }
        return (u64) handle_idx;
    }
    if (num == 10) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            return (u64) -1;
        }
        int target_task = (int) a1;
        if (target_task < 0 || target_task >= g_task_count) {
            return (u64) -1;
        }
        int target_process = g_tasks[target_task].process_index;
        if (target_process < 0) {
            return (u64) -1;
        }
        int obj_index = alloc_object(OBJ_PROCESS, target_process);
        if (obj_index < 0) {
            return (u64) -1;
        }
        int granted_rights = ((int) a2) & RIGHT_QUERY;
        int handle_idx = alloc_handle(caller_process, obj_index, granted_rights);
        if (handle_idx < 0) {
            return (u64) -1;
        }
        return (u64) handle_idx;
    }
    if (num == 11) {
        u8* start;
        u8* end;
        if (!builtin_program_bounds((int) a1, &start, &end)) {
            return (u64) -1;
        }
        int proc_index = spawn_process(start, end, BUILTIN_LOAD_VADDR, BUILTIN_STACK_VADDR);
        if (proc_index < 0) {
            return (u64) -1;
        }
        return (u64) g_processes[proc_index].task_index;
    }
    if (num == 12) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process >= 0) {
            g_processes[caller_process].used = false;
        }
        g_tasks[g_current_task].used = false;
        yield();
        return 0;  // never actually reached - yield() never switches back to an exited task
    }
    return (u64) -1;  // unknown syscall
}
