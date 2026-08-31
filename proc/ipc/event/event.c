// Backs syscalls 74-77 (event_create/wait/signal/reset). event_wait()
// itself lives in kernel/sched/task.c.

#include "event.h"

event g_events[EVENT_SLOTS];

int event_create(void) {
    int i = 0;
    while (i < EVENT_SLOTS) {
        if (!g_events[i].used) {
            g_events[i].used = true;
            g_events[i].signaled = false;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

void event_signal(int index) {
    if (index >= 0 && index < EVENT_SLOTS) {
        g_events[index].signaled = true;
    }
}

void event_reset(int index) {
    if (index >= 0 && index < EVENT_SLOTS) {
        g_events[index].signaled = false;
    }
}
