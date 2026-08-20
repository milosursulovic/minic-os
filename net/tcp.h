#pragma once

#include "../types.h"

#pragma GCC visibility push(hidden)

// A single-shot, client-only TCP fetch: connects to target_ip:target_port
// (real 3-way handshake), sends `request` as one data segment, reads
// back up to max_response_len bytes of whatever the peer replies with
// (across as many segments as arrive within the timeout, ACKing each),
// then attempts a best-effort FIN close. No retransmission, no
// listening/server side, no persistent connection table - see
// net/tcp.c's own comments for the full scope reasoning. Returns true
// only if the handshake completed AND at least one byte of a real reply
// was received; *response_len_out receives the real byte count either
// way (0 on a false return).
bool tcp_fetch(u8* target_ip, u16 target_port, const char* request, u16 request_len,
               u8* response_out, u32 max_response_len, u32* response_len_out);

#pragma GCC visibility pop
