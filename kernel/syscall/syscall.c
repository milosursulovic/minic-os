// int 0x80 syscall gate. rax = number in, return value out; rdi/rsi/rdx
// = up to 3 args. Split across kernel/syscall/handlers/ - one file per
// subsystem, each documenting its own syscall numbers and argument
// conventions in its own top comment. This file is just the flat
// dispatch sequence: each handler returns true (with *result set) if it
// recognizes num, else false, so the next handler gets a turn.

#include "syscall.h"
#include "handlers/core.h"
#include "handlers/process.h"
#include "handlers/vfs.h"
#include "handlers/channel.h"
#include "handlers/io_request.h"
#include "handlers/net_request.h"
#include "handlers/window.h"
#include "handlers/system.h"
#include "handlers/file.h"
#include "handlers/pipe.h"
#include "handlers/shm.h"
#include "handlers/socket.h"
#include "handlers/device.h"
#include "handlers/service_manager.h"
#include "handlers/thread.h"
#include "handlers/sync.h"
#include "handlers/directory.h"
#include "handlers/users.h"
#include "handlers/fork.h"
#include "handlers/port_io.h"

u64 syscall_dispatch(u64 num, u64 a1, u64 a2, u64 a3) {
    u64 result;
    if (syscall_core(num, a1, a2, a3, &result)) return result;
    if (syscall_process(num, a1, a2, a3, &result)) return result;
    if (syscall_vfs(num, a1, a2, a3, &result)) return result;
    if (syscall_channel(num, a1, a2, a3, &result)) return result;
    if (syscall_io_request(num, a1, a2, a3, &result)) return result;
    if (syscall_net_request(num, a1, a2, a3, &result)) return result;
    if (syscall_window(num, a1, a2, a3, &result)) return result;
    if (syscall_system(num, a1, a2, a3, &result)) return result;
    if (syscall_file(num, a1, a2, a3, &result)) return result;
    if (syscall_pipe(num, a1, a2, a3, &result)) return result;
    if (syscall_shm(num, a1, a2, a3, &result)) return result;
    if (syscall_socket(num, a1, a2, a3, &result)) return result;
    if (syscall_device(num, a1, a2, a3, &result)) return result;
    if (syscall_service_manager(num, a1, a2, a3, &result)) return result;
    if (syscall_thread(num, a1, a2, a3, &result)) return result;
    if (syscall_sync(num, a1, a2, a3, &result)) return result;
    if (syscall_directory(num, a1, a2, a3, &result)) return result;
    if (syscall_users(num, a1, a2, a3, &result)) return result;
    if (syscall_fork(num, a1, a2, a3, &result)) return result;
    if (syscall_port_io(num, a1, a2, a3, &result)) return result;
    return (u64) -1;  // unknown syscall
}
