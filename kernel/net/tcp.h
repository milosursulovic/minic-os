#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

// Single-shot client fetch: handshake, send request, read reply, best-effort
// FIN close. Returns true only if the handshake completed and at least one
// byte was received; *response_len_out is set either way.
bool tcp_fetch(u8* target_ip, u16 target_port, const char* request, u16 request_len,
               u8* response_out, u32 max_response_len, u32* response_len_out);

#define TCP_CONNECTION_SLOTS 4

typedef struct {
    bool used;
    u16 local_port;
    u8 remote_ip[4];
    u16 remote_port;
} tcp_connection;

extern tcp_connection g_tcp_connections[TCP_CONNECTION_SLOTS];

#pragma GCC visibility pop
