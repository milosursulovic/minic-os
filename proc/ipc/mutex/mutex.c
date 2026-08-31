// Backs syscalls 78-80 (mutex_create/lock/unlock). lock()/unlock()
// themselves live in kernel/sched/task.c.

#include "mutex.h"

mutex g_mutexes[MUTEX_SLOTS];

int mutex_create(void) {
    int i = 0;
    while (i < MUTEX_SLOTS) {
        if (!g_mutexes[i].used) {
            g_mutexes[i].used = true;
            g_mutexes[i].locked = false;
            g_mutexes[i].owner_task = -1;
            return i;
        }
        i = i + 1;
    }
    return -1;
}
