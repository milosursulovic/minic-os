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
    if (g_channels[channel_index].full) {
        return false;
    }
    g_channels[channel_index].message = value;
    g_channels[channel_index].full = true;
    return true;
}
