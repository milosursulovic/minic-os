#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

// Single-shot client fetch: handshake, send request, read reply, best-effort
// FIN close. Returns true only if the handshake completed and at least one
// byte was received; *response_len_out is set either way.
bool tcp_fetch(u8* target_ip, u16 target_port, const char* request, u16 request_len,
               u8* response_out, u32 max_response_len, u32* response_len_out);

#pragma GCC visibility pop
