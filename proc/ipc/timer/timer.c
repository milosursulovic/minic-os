// Backs syscalls 81-82 (timer_create/wait). wait() itself lives in
// kernel/sched/task.c.

#include "timer.h"
#include "../../../kernel/isr/isr.h"

timer g_timers[TIMER_SLOTS];

int timer_create(u64 duration_ticks) {
    int i = 0;
    while (i < TIMER_SLOTS) {
        if (!g_timers[i].used) {
            g_timers[i].used = true;
            g_timers[i].wake_tick = g_tick_count + duration_ticks;
            return i;
        }
        i = i + 1;
    }
    return -1;
}
