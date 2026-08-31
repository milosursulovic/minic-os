#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real Event object (Faza I point 2) - manual-reset semantics: once
// signaled, stays signaled until explicitly reset (not auto-cleared on a
// single waiter waking) - the simplest real behavior, matching a real
// Win32-style manual-reset event. Blocking wait() itself lives in
// kernel/sched/task.c (needs g_tasks/yield() directly), same split every
// other proc/ipc/ primitive already uses.
#define EVENT_SLOTS 8

typedef struct {
    bool used;
    bool signaled;
} event;

extern event g_events[EVENT_SLOTS];

int event_create(void);
void event_signal(int index);
void event_reset(int index);

#pragma GCC visibility pop
