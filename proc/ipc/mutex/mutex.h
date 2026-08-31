#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Real Mutex object (Faza I point 2). lock()/unlock() themselves live in
// kernel/sched/task.c - lock() needs the same disable_interrupts()-
// protected test-and-set kernel/mm/frames/frames.c's alloc_frame() was
// just fixed with (see its own comment), retried via a cooperative
// yield() loop on contention rather than a hot spin.
#define MUTEX_SLOTS 8

typedef struct {
    bool used;
    bool locked;
    int owner_task;  // -1 = unlocked; only meaningful while locked
} mutex;

extern mutex g_mutexes[MUTEX_SLOTS];

int mutex_create(void);

#pragma GCC visibility pop
