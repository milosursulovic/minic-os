// Kernel object table + per-process handle tables (NT-style). Ring3
// code only ever sees a small integer handle, never a raw object index.

#include "object.h"

kernel_object g_objects[8];
int g_object_count;

handle g_handle_tables[MAX_PROCESSES][8];

int alloc_object(int type, int data_index) {
    int i = 0;
    while (i < 8) {
        if (!g_objects[i].used) {
            g_objects[i].used = true;
            g_objects[i].type = type;
            g_objects[i].data_index = data_index;
            g_object_count = g_object_count + 1;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

void free_object(int object_index) {
    g_objects[object_index].used = false;
    g_object_count = g_object_count - 1;
}

int alloc_handle(int process_index, int object_index, int rights) {
    int i = 0;
    while (i < HANDLES_PER_PROCESS) {
        if (!g_handle_tables[process_index][i].used) {
            g_handle_tables[process_index][i].used = true;
            g_handle_tables[process_index][i].object_index = object_index;
            g_handle_tables[process_index][i].rights = rights;
            return i;
        }
        i = i + 1;
    }
    return -1;
}

void free_handle(int process_index, int handle_index) {
    g_handle_tables[process_index][handle_index].used = false;
}
