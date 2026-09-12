// Backs syscalls 74-77 (event_create/wait/signal/reset). event_wait()
// itself lives in kernel/sched/task.c.

#include "event.h"

// Same disable_interrupts()/restore_interrupts() pattern
// kernel/sched/task.c's yield()/event_wait() use (own copy - no shared
// header, see task.c's own comment) - protects event_signal()'s write to
// the same `signaled` flag event_wait()'s blocked/waiting_on pair watches.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

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
        u64 saved_flags = disable_interrupts();
        g_events[index].signaled = true;
        restore_interrupts(saved_flags);
    }
}

void event_reset(int index) {
    if (index >= 0 && index < EVENT_SLOTS) {
        u64 saved_flags = disable_interrupts();
        g_events[index].signaled = false;
        restore_interrupts(saved_flags);
    }
}
