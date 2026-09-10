#include "core.h"
#include "../../drivers/io/io.h"
#include "../../lib/strings.h"
#include "../../sched/task.h"
#include "../../../proc/ipc/object/object.h"

// 1 print (message + one hex value, tagged with task/process index),
// 13 handle_close (generic across every object type via
// free_object/free_handle), 83/84 handle_grant/handle_grant3 (Faza I
// point 8 - real cross-process handle sharing, generic over any object
// type since it just copies (object_index, rights) into the target's
// own handle table).
bool syscall_core(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    if (num == 1) {
        char* s = (char*) a1;
        serial_print(s);
        vga_print(s);
        print_hex(a2);
        // TEMPORARY diagnostic tag for the button_poll self-corruption
        // investigation (see [[project_button_poll_crash_bug]]) - every
        // syscall-1 debug print now also names which task/process made
        // it, so a bad-pointer report can be tied to a specific ring3
        // program instead of just "something, somewhere". Remove once
        // that bug is root-caused.
        serial_print(" [task=0x");
        print_hex((u64) g_current_task);
        serial_print(" proc=0x");
        print_hex((u64) g_tasks[g_current_task].process_index);
        serial_print("]");
        serial_print("\n");
        *result = 0;
        return true;
    }
    if (num == 13) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            *result = (u64) -1;
            return true;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            *result = (u64) -1;
            return true;
        }
        free_object(g_handle_tables[caller_process][handle_idx].object_index);
        free_handle(caller_process, handle_idx);
        *result = 0;
        return true;
    }
    // Real cross-process handle sharing (Faza I point 8) - a fresh
    // process's handle table starts and stays empty (only handle 0 =
    // self, granted automatically by spawn_process()), completely
    // disconnected from its parent's own handles. Generic (works for any
    // object type, not just SharedMemory - syscall 57's own "map into a
    // target's address space" only covers memory specifically): resolve
    // the caller's own handle to (object_index, rights), grant the SAME
    // object into the target task's own process with the SAME rights.
    if (num == 83) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            *result = (u64) -1;
            return true;
        }
        handle* h = &g_handle_tables[caller_process][handle_idx];
        if (!h->used) {
            *result = (u64) -1;
            return true;
        }
        int target_task_index = (int) a2;
        if (target_task_index < 0 || target_task_index >= MAX_TASKS) {
            *result = (u64) -1;
            return true;
        }
        int target_process = g_tasks[target_task_index].process_index;
        if (target_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int new_handle = alloc_handle(target_process, h->object_index, h->rights);
        *result = new_handle < 0 ? (u64) -1 : (u64) new_handle;
        return true;
    }
    // Atomic 3-handle grant (Faza I point 8's cross-process SharedMemory
    // demo) - three separate syscall 83 calls in a row would leave a
    // real, ring3-observable window (a fresh child scheduled BETWEEN two
    // of the parent's own grant calls sees only a partial grant) since
    // ring3 code is genuinely interruptible between any two `int 0x80`
    // calls even with no explicit yield - this single syscall runs the
    // whole 3-grant sequence under the same interrupt-gate atomicity
    // every syscall already gets, so a waiting child either sees all
    // three handles or none, never a partial set. handle_a -> a1,
    // handle_b/handle_c packed into a2 (high/low 32 bits) since a
    // syscall only carries 3 real argument slots.
    if (num == 84) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int target_task_index = (int) a3;
        if (target_task_index < 0 || target_task_index >= MAX_TASKS) {
            *result = (u64) -1;
            return true;
        }
        int target_process = g_tasks[target_task_index].process_index;
        if (target_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int handles[3];
        handles[0] = (int) a1;
        handles[1] = (int) (a2 >> 32);
        handles[2] = (int) (a2 & 0xFFFFFFFF);
        int i = 0;
        while (i < 3) {
            int hi = handles[i];
            if (hi < 0 || hi >= HANDLES_PER_PROCESS || !g_handle_tables[caller_process][hi].used) {
                *result = (u64) -1;
                return true;
            }
            i = i + 1;
        }
        i = 0;
        while (i < 3) {
            handle* h = &g_handle_tables[caller_process][handles[i]];
            if (alloc_handle(target_process, h->object_index, h->rights) < 0) {
                *result = (u64) -1;
                return true;
            }
            i = i + 1;
        }
        *result = 0;
        return true;
    }
    return false;
}
