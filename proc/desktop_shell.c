// The desktop shell (Faza II point 22): a full-width wallpaper, a
// full-width taskbar with a launcher button and a live uptime label.
// Auto-spawned by kmain.c at boot (Mechanism A - same as init.c/
// hello_service.c, a fixed blob linked into kernel.elf, not a
// runtime-registered service) - unlike ring3prog.c's on-demand demos,
// this one just runs forever from boot, no shell trigger needed.
//
// _start must be at offset 0 - see ring3prog.c's own comment on this;
// same __attribute__((section(".text.start"))) + ring3.ld requirement.

#include "../types.h"
#include "gui_toolkit.h"

#define SCREEN_WIDTH 800
#define SCREEN_HEIGHT 600
#define TASKBAR_HEIGHT 20

#define WALLPAPER_COLOR 0x001A3A5Cu
#define TASKBAR_COLOR 0x00303030u
#define LAUNCHER_NORMAL_COLOR 0x00505050u
#define LAUNCHER_PRESSED_COLOR 0x0000AA00u
#define LABEL_COLOR 0x00FFFFFFu

// How many raw ticks must pass before the uptime label redraws again -
// NOT "redraw on any change". CLAUDE.md's own documented gotcha is that
// QEMU/TCG's PIT timer runs far faster than the nominal 100Hz, so
// g_tick_count can advance on nearly every loop iteration regardless of
// real wall-clock time - "only redraw when the value changed" barely
// throttles anything in practice, since it's almost always true. Gating
// on a real tick DELTA bounds the redraw rate independent of however
// fast TCG happens to be running the timer.
#define UPTIME_REDRAW_TICK_INTERVAL 50

__attribute__((section(".text.start")))
void _start(void) {
    gt_window_create_borderless(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, WALLPAPER_COLOR);
    int taskbar_id = gt_window_create_borderless(0, SCREEN_HEIGHT - TASKBAR_HEIGHT,
                                                  SCREEN_WIDTH, TASKBAR_HEIGHT, TASKBAR_COLOR);

    // Paint the whole taskbar background first - once anything is drawn
    // into a window's content buffer (the button/label below), the flat
    // body_color stops applying and untouched cells default to 0 (black),
    // not TASKBAR_COLOR. Without this the taskbar would render as a black
    // bar with a gray button floating in it, not a solid-colored one.
    gt_window_fill_rect_args bg_args;
    bg_args.id = taskbar_id;
    bg_args.x = 0;
    bg_args.y = 0;
    bg_args.width = SCREEN_WIDTH;
    bg_args.height = TASKBAR_HEIGHT;
    bg_args.color = TASKBAR_COLOR;
    gt_syscall(30, (u64) &bg_args, 0, 0);

    button launcher;
    button_init(&launcher, taskbar_id, 4, 2, 60, 16, "MENU",  // font is uppercase-only
                LAUNCHER_NORMAL_COLOR, LAUNCHER_PRESSED_COLOR, LABEL_COLOR);

    // Sentinel: no real tick count is ever this value on a fresh boot, so
    // the first loop iteration always draws the label once.
    u64 last_rendered_ticks = (u64) -1;

    for (;;) {
        bool clicked = button_poll(&launcher);
        if (clicked) {
            // Proves the launcher genuinely launches something visible,
            // not just a color change - one fixed demo app window per
            // click (a real app menu is future work, see the roadmap
            // memory's documented limitation for this milestone).
            gt_window_create(250, 150, 200, 150, 0x00444444u, 0x00888888u);
        }

        // See UPTIME_REDRAW_TICK_INTERVAL above - unthrottled, this was
        // consuming enough CPU via the round-robin scheduler (each
        // redraw triggers a full compositor_redraw(), an 800x600
        // back-buffer blit, not cheap) to effectively starve other ring3
        // tasks (proc/terminal.c's own redraw loop in particular) of
        // real wall-clock progress, so the terminal window's mirrored
        // text never visibly updated in interactive use even though the
        // underlying pipeline worked.
        u64 current_ticks = gt_get_ticks();
        if (last_rendered_ticks == (u64) -1
            || current_ticks - last_rendered_ticks >= UPTIME_REDRAW_TICK_INTERVAL) {
            char uptime_text[24];
            int i = 0;
            uptime_text[i] = 'U'; i = i + 1;
            uptime_text[i] = 'P'; i = i + 1;
            uptime_text[i] = ' '; i = i + 1;
            uptime_text[i] = '0'; i = i + 1;
            uptime_text[i] = 'X'; i = i + 1;  // font only has uppercase - lowercase 'x' renders blank
            gt_format_hex(current_ticks, &uptime_text[i]);

            gt_window_draw_text_args label_args;
            label_args.id = taskbar_id;
            label_args.x = 700;
            label_args.y = 6;
            label_args.fg_color = LABEL_COLOR;
            label_args.bg_color = TASKBAR_COLOR;
            label_args.text = &uptime_text[0];
            gt_syscall(32, (u64) &label_args, 0, 0);

            last_rendered_ticks = current_ticks;
        }
    }
}
