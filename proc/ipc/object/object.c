// Kernel object table + per-process handle tables (NT-style). Ring3
// code only ever sees a small integer handle, never a raw object index.

#include "object.h"

kernel_object g_objects[OBJECT_SLOTS];
int g_object_count;

handle g_handle_tables[MAX_PROCESSES][8];

// Save/restore IF - same established pattern kernel/mm/frames/frames.c's
// alloc_frame()/kernel/mm/heap/heap.c's kalloc() use, for the identical
// bug class ([[project_mouse_keyboard_race_bug]]): a scan-then-mutate
// free-slot search with no atomicity. Every caller here runs from a
// syscall handler, so two different processes' syscalls (one interrupted
// mid-scan, a preempting timer tick handing control to the other before
// the first ever sets used=true) could both claim the SAME object or
// handle slot - aliasing two unrelated kernel objects, or two unrelated
// processes' handle tables, onto one shared slot.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

int alloc_object(int type, int data_index) {
    u64 saved_flags = disable_interrupts();
    int i = 0;
    while (i < OBJECT_SLOTS) {
        if (!g_objects[i].used) {
            g_objects[i].used = true;
            g_objects[i].type = type;
            g_objects[i].data_index = data_index;
            g_object_count = g_object_count + 1;
            restore_interrupts(saved_flags);
            return i;
        }
        i = i + 1;
    }
    restore_interrupts(saved_flags);
    return -1;
}

void free_object(int object_index) {
    u64 saved_flags = disable_interrupts();
    g_objects[object_index].used = false;
    g_object_count = g_object_count - 1;
    restore_interrupts(saved_flags);
}

int alloc_handle(int process_index, int object_index, int rights) {
    u64 saved_flags = disable_interrupts();
    int i = 0;
    while (i < HANDLES_PER_PROCESS) {
        if (!g_handle_tables[process_index][i].used) {
            g_handle_tables[process_index][i].used = true;
            g_handle_tables[process_index][i].object_index = object_index;
            g_handle_tables[process_index][i].rights = rights;
            restore_interrupts(saved_flags);
            return i;
        }
        i = i + 1;
    }
    restore_interrupts(saved_flags);
    return -1;
}

void free_handle(int process_index, int handle_index) {
    u64 saved_flags = disable_interrupts();
    g_handle_tables[process_index][handle_index].used = false;
    restore_interrupts(saved_flags);
}
