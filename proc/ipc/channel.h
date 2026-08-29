#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

typedef struct {
    bool used;
    bool full;
    u64 message;
} channel;

extern channel g_channels[4];
extern int g_channel_count;

int create_channel(void);
bool channel_has_message(int channel_index);
bool channel_send(int channel_index, u64 value);

#pragma GCC visibility pop
