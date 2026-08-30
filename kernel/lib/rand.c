#include "rand.h"
#include "../isr/isr.h"
#include "../drivers/rtc/rtc.h"

static u32 g_rand_state;
static bool g_rand_initialized;

u32 rand_next(void) {
    if (!g_rand_initialized) {
        u8 hour, minute, second;
        rtc_read_time(&hour, &minute, &second);
        u32 seed = (u32) g_tick_count ^ ((u32) hour << 16) ^ ((u32) minute << 8) ^ (u32) second;
        g_rand_state = seed != 0 ? seed : 0xA5A5A5A5;  // xorshift can't start at 0
        g_rand_initialized = true;
    }
    // xorshift32 (Marsaglia) - real, well-known constants, not "porting code".
    g_rand_state ^= g_rand_state << 13;
    g_rand_state ^= g_rand_state >> 17;
    g_rand_state ^= g_rand_state << 5;
    return g_rand_state;
}

u64 randomize_load_vaddr(u64 base) {
    return base + ((u64) (rand_next() % ASLR_SLOTS) * 4096);
}
