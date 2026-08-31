#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real Timer object (Faza I point 2) - a real object other code can wait
// on, wrapping the same wake_tick blocking path kernel/sched/task.c's
// sleep_ticks() already uses (no new task-struct fields needed). wait()
// itself lives in kernel/sched/task.c.
#define TIMER_SLOTS 8

typedef struct {
    bool used;
    u64 wake_tick;
} timer;

extern timer g_timers[TIMER_SLOTS];

int timer_create(u64 duration_ticks);

#pragma GCC visibility pop
