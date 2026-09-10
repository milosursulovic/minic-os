#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Faza I point 8 item 4: message payload is a real byte buffer, not just
// one u64 - channel_send()/channel_receive() (below/task.c) stay as thin
// 8-byte wrappers over channel_send_msg()/channel_take_msg() so every
// existing u64-trigger call site is unaffected.
#define CHANNEL_MSG_MAX 128

typedef struct {
    bool used;
    bool full;
    u32 msg_len;
    u8 msg_data[CHANNEL_MSG_MAX];
} channel;

extern channel g_channels[4];
extern int g_channel_count;

int create_channel(void);
bool channel_has_message(int channel_index);
bool channel_send(int channel_index, u64 value);
// Non-blocking. Fails if the slot is full or len exceeds CHANNEL_MSG_MAX.
bool channel_send_msg(int channel_index, const void* data, u32 len);
// Caller must already know the slot is full (task.c's blocking wait
// checks channel_has_message() first). Copies min(msg_len,max_len) bytes,
// clears full, returns the REAL msg_len (lets the caller detect truncation
// even when max_len was smaller than what was actually sent).
u32 channel_take_msg(int channel_index, void* buf, u32 max_len);

#pragma GCC visibility pop
