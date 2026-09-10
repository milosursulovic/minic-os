#include "sync.h"
#include "../../sched/task.h"
#include "../../../proc/ipc/object/object.h"
#include "../../../proc/ipc/event/event.h"
#include "../../../proc/ipc/mutex/mutex.h"
#include "../../../proc/ipc/timer/timer.h"

// Real Event/Mutex/Timer objects (Faza I point 2) - give the Thread
// object something real to coordinate/exclude over. Each
// _wait/_lock/_signal/_reset/_unlock call validates its handle is the
// right OBJ_* type first, same shape syscall 72 (thread_join) uses.
bool syscall_sync(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    (void) a2;
    (void) a3;
    if (num == 74) {
        if (g_tasks[g_current_task].process_index < 0) {
            *result = (u64) -1;
            return true;
        }
        int slot = event_create();
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj = alloc_object(OBJ_EVENT, slot);
        if (obj < 0) {
            *result = (u64) -1;
            return true;
        }
        int h = alloc_handle(g_tasks[g_current_task].process_index, obj, RIGHT_QUERY);
        *result = h < 0 ? (u64) -1 : (u64) h;
        return true;
    }
    if (num == 75 || num == 76 || num == 77) {
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
        kernel_object* obj = &g_objects[h->object_index];
        if (obj->type != OBJ_EVENT) {
            *result = (u64) -1;
            return true;
        }
        if (num == 75) {
            event_wait(obj->data_index);
        } else if (num == 76) {
            event_signal(obj->data_index);
        } else {
            event_reset(obj->data_index);
        }
        *result = 0;
        return true;
    }
    if (num == 78) {
        if (g_tasks[g_current_task].process_index < 0) {
            *result = (u64) -1;
            return true;
        }
        int slot = mutex_create();
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj = alloc_object(OBJ_MUTEX, slot);
        if (obj < 0) {
            *result = (u64) -1;
            return true;
        }
        int h = alloc_handle(g_tasks[g_current_task].process_index, obj, RIGHT_QUERY);
        *result = h < 0 ? (u64) -1 : (u64) h;
        return true;
    }
    if (num == 79 || num == 80) {
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
        kernel_object* obj = &g_objects[h->object_index];
        if (obj->type != OBJ_MUTEX) {
            *result = (u64) -1;
            return true;
        }
        if (num == 79) {
            mutex_lock(obj->data_index);
        } else {
            mutex_unlock(obj->data_index);
        }
        *result = 0;
        return true;
    }
    if (num == 81) {
        if (g_tasks[g_current_task].process_index < 0) {
            *result = (u64) -1;
            return true;
        }
        int slot = timer_create(a1);
        if (slot < 0) {
            *result = (u64) -1;
            return true;
        }
        int obj = alloc_object(OBJ_TIMER, slot);
        if (obj < 0) {
            *result = (u64) -1;
            return true;
        }
        int h = alloc_handle(g_tasks[g_current_task].process_index, obj, RIGHT_QUERY);
        *result = h < 0 ? (u64) -1 : (u64) h;
        return true;
    }
    if (num == 82) {
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
        kernel_object* obj = &g_objects[h->object_index];
        if (obj->type != OBJ_TIMER) {
            *result = (u64) -1;
            return true;
        }
        timer_wait(obj->data_index);
        *result = 0;
        return true;
    }
    return false;
}
