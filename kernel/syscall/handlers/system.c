#include "system.h"
#include "common.h"
#include "../../isr/isr.h"
#include "../../drivers/io/io.h"
#include "../../fs/minifs/minifs.h"
#include "../../mm/frames/frames.h"
#include "../../lib/rand.h"
#include "../../../proc/process.h"

#pragma GCC visibility push(hidden)
extern u8 g_terminal_prog_start;
extern u8 g_terminal_prog_end;
extern u8 g_file_manager_prog_start;
extern u8 g_file_manager_prog_end;
extern u8 g_settings_prog_start;
extern u8 g_settings_prog_end;
extern u8 g_device_manager_prog_start;
extern u8 g_device_manager_prog_end;
extern u8 g_service_manager_prog_start;
extern u8 g_service_manager_prog_end;
#pragma GCC visibility pop

typedef struct __attribute__((packed)) {
    u64 since_pos;
    char* out_buf;
    u32 max_len;
} term_read_args;

typedef struct __attribute__((packed)) {
    u32* total_frames_out;
    u32* free_frames_out;
    u32* disk_file_count_out;
} sys_info_args;

// Deliberately separate from handlers/process.c's builtin_program_bounds()
// - that one is the hello_service/register_service IPC demo registry,
// this one is the fixed set of compiled-in GUI apps desktop_shell.c's
// MENU dropdown can launch on demand instead of kmain.c auto-spawning
// all of them at boot.
static bool gui_app_bounds(int app_id, u8** start_out, u8** end_out) {
    if (app_id == 0) {
        *start_out = &g_terminal_prog_start;
        *end_out = &g_terminal_prog_end;
        return true;
    }
    if (app_id == 1) {
        *start_out = &g_file_manager_prog_start;
        *end_out = &g_file_manager_prog_end;
        return true;
    }
    if (app_id == 2) {
        *start_out = &g_settings_prog_start;
        *end_out = &g_settings_prog_end;
        return true;
    }
    if (app_id == 3) {
        *start_out = &g_device_manager_prog_start;
        *end_out = &g_device_manager_prog_end;
        return true;
    }
    if (app_id == 4) {
        *start_out = &g_service_manager_prog_start;
        *end_out = &g_service_manager_prog_end;
        return true;
    }
    return false;
}

bool syscall_system(u64 num, u64 a1, u64 a2, u64 a3, u64* result) {
    (void) a2;
    (void) a3;
    if (num == 35) {
        *result = g_tick_count;
        return true;
    }
    if (num == 36) {
        term_read_args* args = (term_read_args*) a1;
        u64 since_pos = args->since_pos;
        // Caller fell behind further than the buffer holds - clamp to the
        // oldest byte still available. A caller that was already behind
        // before this clamp and tracks its own position as since_pos+copied
        // will re-sync from here; with a 16KB buffer and a caller that
        // polls every loop iteration, this is not expected to trigger in
        // practice - not resolved further than that.
        if (g_term_write_pos > TERM_SCROLLBACK_SIZE
            && since_pos < g_term_write_pos - TERM_SCROLLBACK_SIZE) {
            since_pos = g_term_write_pos - TERM_SCROLLBACK_SIZE;
        }
        u32 copied = 0;
        while (since_pos < g_term_write_pos && copied < args->max_len) {
            args->out_buf[copied] = g_term_scrollback[since_pos % TERM_SCROLLBACK_SIZE];
            since_pos = since_pos + 1;
            copied = copied + 1;
        }
        *result = (u64) copied;
        return true;
    }
    if (num == 40) {
        sys_info_args* args = (sys_info_args*) a1;
        *args->total_frames_out = g_total_frames;
        *args->free_frames_out = g_free_frame_count;
        u32 file_count;
        fs_superblock_info(&file_count);
        *args->disk_file_count_out = file_count;
        *result = 0;
        return true;
    }
    if (num == 41) {
        u8* start;
        u8* end;
        if (!gui_app_bounds((int) a1, &start, &end)) {
            *result = (u64) -1;
            return true;
        }
        u64 load_vaddr = randomize_load_vaddr(BUILTIN_LOAD_BASE);
        int proc_index = spawn_process(start, end, load_vaddr, load_vaddr + 0x20000);
        *result = (u64) proc_index;  // spawn_process's own -1-on-failure convention
        return true;
    }
    // Syscalls 42/43 (direct kernel-side rtc_read_time/rtc_read_date)
    // removed - Faza I point 14, item 14: real ring3 driver isolation.
    // There is now exactly one way for a ring3 process to get wall-clock
    // time, and it goes through the isolated driver (proc/drivers/
    // rtc_driver/rtc_driver.c) - no raw-port bypass for an arbitrary
    // process. The kernel's own internal need (kernel/lib/rand.c's ASLR
    // seed) still calls rtc_read_time() directly, unaffected by this.
    return false;
}
