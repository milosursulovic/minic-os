#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

// A small, honestly-scoped PRNG - same tone as kernel/net/tcp/tcp.c's own
// "tick-derived, not cryptographically random - fine, nothing here needs
// that" precedent. Used for real ASLR (ADDR 4+): defeats a hardcoded-
// load-address assumption, not a security-grade secrecy requirement.

// xorshift32, lazily seeded once from live boot state (g_tick_count XOR
// the real CMOS RTC time) so two closely-spaced boots don't reseed
// identically just because the tick count resets to a similar small
// value on its own.
u32 rand_next(void);

// base + a random page-aligned offset within ASLR_SLOTS pages - see
// kernel/services/service_manager.c/kernel/syscall/syscall.c/kmain.c for
// real call sites (every place THIS kernel decides where to load a
// process, as opposed to a ring3-controlled address like syscall 6).
#define ASLR_SLOTS 512  // 512 * 4096 = 2MB window
u64 randomize_load_vaddr(u64 base);

#pragma GCC visibility pop
