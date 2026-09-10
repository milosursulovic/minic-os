#pragma once
#include "../../types.h"

// Low-level primitives every other proc/gui_toolkit/*.h file builds on:
// the raw int 0x80 syscall trampoline, a self-contained hex formatter
// (kernel/lib/strings.c's format_hex() isn't linked into ring3 programs -
// each is its own standalone-linked blob, see proc/ring3.ld), and a
// debug-print wrapper. Split out of the former single gui_toolkit.h.

static u64 gt_syscall(u64 num, u64 arg1, u64 arg2, u64 arg3) {
    u64 result;
    register u64 r_num __asm__("rax") = num;
    register u64 r_arg1 __asm__("rdi") = arg1;
    register u64 r_arg2 __asm__("rsi") = arg2;
    register u64 r_arg3 __asm__("rdx") = arg3;
    __asm__ volatile("int $0x80"
                      : "+r"(r_num)
                      : "r"(r_arg1), "r"(r_arg2), "r"(r_arg3)
                      : "memory");
    result = r_num;
    return result;
}

// Null-terminates, unlike format_hex(), since window_draw_text's syscall
// dereferences a null-terminated string on the kernel side. Returns the
// digit count, not counting the terminator. Uppercase A-F, not lowercase -
// the font (kernel/gfx/font.h) has no lowercase glyphs at all, so a
// lowercase hex digit used to render as an invisible gap (found via
// Settings' memory stats, the first caller whose values routinely land
// above 0xF - desktop_shell's tick counts and File Manager's sizes
// happened to stay in the 0-9 range in every case tested so far, masking
// this until now).
static __attribute__((unused)) int gt_format_hex(u64 value, char* out) {
    const char* digits = "0123456789ABCDEF";
    if (value == 0) {
        out[0] = '0';
        out[1] = '\0';
        return 1;
    }
    char buf[16];
    int i = 15;
    while (value > 0 && i >= 0) {
        buf[i] = digits[value % 16];
        value = value / 16;
        i = i - 1;
    }
    int len = 15 - i;
    int j = 0;
    while (j < len) {
        out[j] = buf[i + 1 + j];
        j = j + 1;
    }
    out[len] = '\0';
    return len;
}

// TEMPORARY diagnostic helper for the button_poll self-corruption
// investigation ([[project_button_poll_crash_bug]] in project memory) -
// wraps syscall 1 (message + one hex value, tagged with task/process
// index kernel-side). Remove once that bug is root-caused.
static __attribute__((unused)) void gt_debug_print(const char* msg, u64 value) {
    gt_syscall(1, (u64) msg, value, 0);
}
