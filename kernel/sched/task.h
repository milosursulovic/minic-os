#pragma once

#include "../../types.h"

#pragma GCC visibility push(hidden)

// 17 tasks are already needed at boot after the Settings app (11 fixed
// kernel tasks + 6 spawn_process boot processes - test_prog/init/
// desktop_shell/terminal/file_manager/settings) - real headroom beyond
// that, not another exact-fit, so the shell's own spawn/ring3* demo
// commands still have room.
#define MAX_TASKS 24

typedef struct {
    u64 saved_rsp;
    bool used;
    bool blocked;
    u64 wake_tick;           // only meaningful while blocked and waiting_on == NULL
    u64 cr3;                 // this task's address space
    u64 ring3_entry_vaddr;   // 0 = plain kernel task; else jump here via run_ring3_test
    u64 ring3_user_stack_top;  // only meaningful when ring3_entry_vaddr != 0
    int process_index;       // -1 = plain kernel task; else an index into g_processes[]
    bool* waiting_on;         // NULL = blocked on wake_tick; else wakes once *waiting_on is true
    u64 kernel_stack_top;    // this task's own TSS.RSP0 target - see set_tss_rsp0's comment
} task;

extern task g_tasks[MAX_TASKS];
extern int g_task_count;
extern int g_current_task;

extern u64 g_task1_ticks;
extern u64 g_task2_ticks;
extern u64 g_task3_ticks;
extern u64 g_task4_ticks;

extern u64 g_demo_vaddr;
extern u32 g_proc_a_value;
extern u32 g_proc_b_value;
extern u64 g_proc_a_phys;
extern u64 g_proc_b_phys;

extern int g_channel_demo;
extern bool g_receiver_got_message;
extern u64 g_receiver_value;
extern int g_ring3_channel_demo;
// Boot-time well-known Pipe index - same "creation order fixes the
// index" convention as g_channel_demo/g_ring3_channel_demo above, see
// kmain.c.
extern int g_ring3_pipe_demo;

void scheduler_init(void);
int create_task_with_cr3(void (*entry)(void), u64 cr3);
bool create_task(void (*entry)(void));
bool create_isolated_task(void (*entry)(void));
void yield(void);
void thread_join(int target_task_index);
void event_wait(int index);
void mutex_lock(int index);
void mutex_unlock(int index);
void timer_wait(int index);
void sleep_ticks(u64 ticks);
u64 channel_receive(int channel_index);
void io_request_wait(int slot_index);
void net_ping_request_wait(int slot_index);
void net_tcp_request_wait(int slot_index);

void task1_entry(void);
void task2_entry(void);
void task3_entry(void);
void task4_entry(void);
void proc_a_entry(void);
void proc_b_entry(void);
void proc_receiver_entry(void);

#pragma GCC visibility pop
