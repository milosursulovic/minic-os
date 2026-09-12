// IPC channels: single-slot mailboxes. channel_receive() lives in
// task.c (needs g_current_task/yield()). send() is non-blocking - fails
// if full rather than blocking the sender too.

#include "channel.h"

// Same disable_interrupts()/restore_interrupts() pattern
// kernel/sched/task.c's yield()/mutex_lock() use (own copy - no shared
// header, see that file's own comment). channel_send_msg()'s
// check-then-act on `full` (and channel_take_msg()'s matching
// read-then-clear) is a real race without it: a receiver blocked in
// task.c's channel_receive()/channel_receive_msg() sets its own
// blocked/waiting_on pair under this same protection - a sender/taker
// racing the `full` flag without it could interleave with that.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

channel g_channels[4];
int g_channel_count;

int create_channel(void) {
    if (g_channel_count >= 4) {
        return -1;
    }
    int idx = g_channel_count;
    g_channels[idx].used = true;
    g_channels[idx].full = false;
    g_channel_count = g_channel_count + 1;
    return idx;
}

bool channel_has_message(int channel_index) {
    return g_channels[channel_index].full;
}

bool channel_send(int channel_index, u64 value) {
    return channel_send_msg(channel_index, &value, sizeof(value));
}

bool channel_send_msg(int channel_index, const void* data, u32 len) {
    if (len > CHANNEL_MSG_MAX) {
        return false;
    }
    u64 saved_flags = disable_interrupts();
    if (g_channels[channel_index].full) {
        restore_interrupts(saved_flags);
        return false;
    }
    u8* src = (u8*) data;
    u32 i = 0;
    while (i < len) {
        g_channels[channel_index].msg_data[i] = src[i];
        i = i + 1;
    }
    g_channels[channel_index].msg_len = len;
    g_channels[channel_index].full = true;
    restore_interrupts(saved_flags);
    return true;
}

u32 channel_take_msg(int channel_index, void* buf, u32 max_len) {
    u64 saved_flags = disable_interrupts();
    u32 real_len = g_channels[channel_index].msg_len;
    u32 copy_len = real_len < max_len ? real_len : max_len;
    u8* dst = (u8*) buf;
    u32 i = 0;
    while (i < copy_len) {
        dst[i] = g_channels[channel_index].msg_data[i];
        i = i + 1;
    }
    g_channels[channel_index].full = false;
    restore_interrupts(saved_flags);
    return real_len;
}
