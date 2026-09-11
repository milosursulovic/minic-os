#include "port_io.h"
#include "../../sched/task.h"
#include "../../../proc/ipc/object/object.h"
#include "../../drivers/io_port_range/io_port_range.h"
#include "../../drivers/io/io.h"

bool syscall_port_io(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    if (num == 99) {
        int caller_process = g_tasks[g_current_task].process_index;
        if (caller_process < 0) {
            *result = (u64) -1;
            return true;
        }
        int handle_idx = (int) a1;
        if (handle_idx < 0 || handle_idx >= HANDLES_PER_PROCESS) {
            *result = (u64) -1;
            return true;
        }
        if (!g_handle_tables[caller_process][handle_idx].used) {
            *result = (u64) -1;
            return true;
        }
        int op = (int) a2;
        int required_right = op == 0 ? RIGHT_READ : RIGHT_WRITE;
        if ((g_handle_tables[caller_process][handle_idx].rights & required_right) == 0) {
            *result = (u64) -1;
            return true;
        }
        int obj_index = g_handle_tables[caller_process][handle_idx].object_index;
        if (g_objects[obj_index].type != OBJ_IO_PORT_RANGE) {
            *result = (u64) -1;
            return true;
        }
        int slot = g_objects[obj_index].data_index;
        u16 port = (u16) (a3 & 0xFFFF);
        if (!io_port_range_contains(slot, port)) {
            *result = (u64) -1;
            return true;
        }
        if (op == 0) {
            *result = (u64) inb(port);
            return true;
        }
        if (op == 1) {
            u8 value = (u8) ((a3 >> 16) & 0xFF);
            outb(port, value);
            *result = 0;
            return true;
        }
        *result = (u64) -1;
        return true;
    }
    return false;
}
