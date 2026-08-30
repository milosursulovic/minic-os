#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// A real generic Socket object - a thin wrapper mapping ring3 handles
// onto kernel/net/tcp/tcp.c's real listener/connection slots, same shape
// as proc/ipc/file/file.c wrapping MiniFS. Backs syscalls 59-63.
#define SOCKET_SLOTS 4

typedef struct {
    bool used;
    bool listening;  // true = tcp_slot indexes g_tcp_listeners[]; false = g_tcp_connections[]
    int tcp_slot;
} socket_entry;

extern socket_entry g_sockets[SOCKET_SLOTS];

int socket_create_listener(u16 port);
// Blocks up to timeout_ticks for a real client; returns a NEW socket
// slot for the accepted (established) connection, or -1.
int socket_accept(int socket_slot, u64 timeout_ticks, u8* remote_ip_out, u16* remote_port_out);
u32 socket_send(int socket_slot, const u8* data, u16 len);
u32 socket_receive(int socket_slot, u8* buf, u32 max_len, u64 timeout_ticks);
void socket_close(int socket_slot);

#pragma GCC visibility pop
