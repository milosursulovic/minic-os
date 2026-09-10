#pragma once

#pragma GCC visibility push(hidden)

void cmd_ring3_go(void);
void cmd_ring3_fault(void);
void cmd_ring3_nx(void);
void cmd_ring3_register(void);
void cmd_ring3_unregister(void);
void cmd_ring3_async(void);
void cmd_ring3_async_write(void);
void cmd_ring3_async_ping(void);
void cmd_ring3_async_dns(void);
void cmd_ring3_async_tcp(void);
void cmd_ring3_window(void);
void cmd_ring3_mouse(void);
void cmd_ring3_text(void);
void cmd_ring3_button(void);
void cmd_ring3_file_object(void);
void cmd_ring3_perms(void);
void cmd_ring3_posix(void);
void cmd_ring3_pipe(void);
void cmd_ring3_shm(void);
void cmd_ring3_tcp_server(void);
void cmd_ring3_widgets(void);
void cmd_ring3_focus(void);
void cmd_ring3_thread(void);
void cmd_ring3_sync(void);
void cmd_ring3_shm_sync(void);
void cmd_ring3_msg(void);
void cmd_ring3_objs(void);
void cmd_ring3_users(void);
void cmd_ring3_vfs_perm(void);

#pragma GCC visibility pop
