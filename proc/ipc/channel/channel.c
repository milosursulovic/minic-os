// IPC channels: single-slot mailboxes. channel_receive() lives in
// task.c (needs g_current_task/yield()). send() is non-blocking - fails
// if full rather than blocking the sender too.

#include "channel.h"

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
    if (g_channels[channel_index].full) {
        return false;
    }
    if (len > CHANNEL_MSG_MAX) {
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
    return true;
}

u32 channel_take_msg(int channel_index, void* buf, u32 max_len) {
    u32 real_len = g_channels[channel_index].msg_len;
    u32 copy_len = real_len < max_len ? real_len : max_len;
    u8* dst = (u8*) buf;
    u32 i = 0;
    while (i < copy_len) {
        dst[i] = g_channels[channel_index].msg_data[i];
        i = i + 1;
    }
    g_channels[channel_index].full = false;
    return real_len;
}
