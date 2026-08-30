#include "socket.h"
#include "../../../kernel/net/tcp/tcp.h"

socket_entry g_sockets[SOCKET_SLOTS];

static int find_free_slot(void) {
    int i = 0;
    while (i < SOCKET_SLOTS) {
        if (!g_sockets[i].used) {
            return i;
        }
        i = i + 1;
    }
    return -1;
}

int socket_create_listener(u16 port) {
    int listener = tcp_listen(port);
    if (listener < 0) {
        return -1;
    }
    int slot = find_free_slot();
    if (slot < 0) {
        return -1;
    }
    g_sockets[slot].used = true;
    g_sockets[slot].listening = true;
    g_sockets[slot].tcp_slot = listener;
    return slot;
}

int socket_accept(int socket_slot, u64 timeout_ticks, u8* remote_ip_out, u16* remote_port_out) {
    if (!g_sockets[socket_slot].listening) {
        return -1;
    }
    int conn = tcp_accept(g_sockets[socket_slot].tcp_slot, timeout_ticks, remote_ip_out, remote_port_out);
    if (conn < 0) {
        return -1;
    }
    int slot = find_free_slot();
    if (slot < 0) {
        return -1;
    }
    g_sockets[slot].used = true;
    g_sockets[slot].listening = false;
    g_sockets[slot].tcp_slot = conn;
    return slot;
}

u32 socket_send(int socket_slot, const u8* data, u16 len) {
    if (g_sockets[socket_slot].listening) {
        return 0;
    }
    return tcp_server_send(g_sockets[socket_slot].tcp_slot, data, len);
}

u32 socket_receive(int socket_slot, u8* buf, u32 max_len, u64 timeout_ticks) {
    if (g_sockets[socket_slot].listening) {
        return 0;
    }
    return tcp_server_receive(g_sockets[socket_slot].tcp_slot, buf, max_len, timeout_ticks);
}

void socket_close(int socket_slot) {
    if (!g_sockets[socket_slot].listening) {
        tcp_server_close(g_sockets[socket_slot].tcp_slot);
    }
    g_sockets[socket_slot].used = false;
}
