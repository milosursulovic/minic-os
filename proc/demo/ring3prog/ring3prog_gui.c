#include "ring3prog_common.h"

// Extracted from the former single ring3prog.c (1380 lines) - see that
// file's own header comment and ring3prog_common.h for the split rationale.
// Returns true if trigger_value matched one of this group's own triggers
// (and was therefore handled - some branches below never return at all,
// looping forever, exactly as in the original single-file version).
bool run_trigger_gui(u64 trigger_value) {
    if (trigger_value == 11) {
        // trigger 11 (ring3win): create/raise/move/close real windows via syscalls.
        window a;
        bool created_a = window_create(&a, 50, 50, 200, 150, 0x00FF0000, 0x00800000);
        do_syscall(1, (u64) "window_create(a) ok=0x", (u64) created_a, 0);
        do_syscall(1, (u64) "window a.id=0x", (u64) a.id, 0);

        window b;
        bool created_b = window_create(&b, 150, 120, 200, 150, 0x0000FF00, 0x00008000);
        do_syscall(1, (u64) "window_create(b) ok=0x", (u64) created_b, 0);
        do_syscall(1, (u64) "window b.id=0x", (u64) b.id, 0);

        window c;
        bool created_c = window_create(&c, 600, 50, 100, 100, 0x000000FF, 0x00000080);
        do_syscall(1, (u64) "window_create(c) ok=0x", (u64) created_c, 0);
        do_syscall(1, (u64) "window c.id=0x", (u64) c.id, 0);

        bool raised = window_raise(&a);
        do_syscall(1, (u64) "window_raise(a) ok=0x", (u64) raised, 0);

        bool moved = window_move(&c, 600, 400);
        do_syscall(1, (u64) "window_move(c) ok=0x", (u64) moved, 0);

        bool closed = window_close(&c);
        do_syscall(1, (u64) "window_close(c) ok=0x", (u64) closed, 0);

        // Real app-drawn content, not a flat placeholder - a base fill plus
        // an accent rect, left open (unlike c) so its result is checkable.
        window d;
        bool created_d = window_create(&d, 50, 300, 150, 150, 0x00444444, 0x00222222);
        do_syscall(1, (u64) "window_create(d) ok=0x", (u64) created_d, 0);
        do_syscall(1, (u64) "window d.id=0x", (u64) d.id, 0);

        bool filled_base = window_fill_rect(&d, 0, 0, 150, 130, 0x00333333);
        do_syscall(1, (u64) "window_fill_rect(d, base) ok=0x", (u64) filled_base, 0);
        bool filled_accent = window_fill_rect(&d, 20, 20, 50, 50, 0x00FFFF00);
        do_syscall(1, (u64) "window_fill_rect(d, accent) ok=0x", (u64) filled_accent, 0);
    } else if (trigger_value == 12) {
        // trigger 12 (ring3mouse): polls real mouse state 3 times, real work
        // in between - proves it tracks live state, not one frozen snapshot.
        int p = 0;
        while (p < 3) {
            mouse_state m;
            mouse_query(&m);
            do_syscall(1, (u64) "mouse_query() x=0x", (u64) m.x, 0);
            do_syscall(1, (u64) "mouse_query() y=0x", (u64) m.y, 0);
            do_syscall(1, (u64) "mouse_query() buttons=0x", (u64) m.buttons, 0);

            int q = 0;
            while (q < 3) {
                do_syscall(1, (u64) "doing other work, iteration 0x", (u64) q, 0);
                q = q + 1;
            }
            p = p + 1;
        }
    } else if (trigger_value == 13) {
        // trigger 13 (ring3text): draws real text into a window via the new
        // window_draw_text syscall - end-to-end through the same clipping/
        // content-buffer/compositor path window_fill_rect already proved.
        window e;
        bool created_e = window_create(&e, 300, 300, 150, 100, 0x00444444, 0x00222222);
        do_syscall(1, (u64) "window_create(e) ok=0x", (u64) created_e, 0);
        do_syscall(1, (u64) "window e.id=0x", (u64) e.id, 0);

        bool drew_text = window_draw_text(&e, 10, 10, "OS", 0x00FFFFFF, 0x00444444);
        do_syscall(1, (u64) "window_draw_text(e) ok=0x", (u64) drew_text, 0);
    } else if (trigger_value == 14) {
        // trigger 14 (ring3button): a real interactive Button widget
        // (gui_toolkit.h) - polls real mouse+window state via syscalls
        // 31/33, hit-tests, and renders a pressed/normal visual state.
        window f;
        bool created_f = window_create(&f, 400, 200, 200, 120, 0x00444444, 0x00222222);
        do_syscall(1, (u64) "window_create(f) ok=0x", (u64) created_f, 0);
        do_syscall(1, (u64) "window f.id=0x", (u64) f.id, 0);

        button btn;
        button_init(&btn, f.id, 20, 30, 100, 30, "OK", 0x00888888, 0x0000FF00, 0x00FFFFFF);

        int p = 0;
        while (p < 6) {
            bool clicked = button_poll(&btn);
            do_syscall(1, (u64) "button_poll() clicked=0x", (u64) clicked, 0);
            int q = 0;
            while (q < 3) {
                do_syscall(1, (u64) "doing other work, iteration 0x", (u64) q, 0);
                q = q + 1;
            }
            p = p + 1;
        }
    } else if (trigger_value == 21) {
        // trigger 21 (ring3widgets): real lowercase font glyphs
        // (kernel/gfx/font.c) plus six new gui_toolkit.h widgets - Label
        // (consolidates the draw_static_label/draw_row_text duplication
        // already in settings.c/device_manager.c), Checkbox (a real
        // persistent-toggle interaction model, unlike Button's momentary
        // press), RadioButton (exclusive-selection variant of Checkbox),
        // ProgressBar (non-interactive), Slider (real continuous
        // click-and-drag-to-scrub, not click-edge-detection), and
        // ListView (extracted from file_manager.c/service_manager.c's own
        // independently-duplicated row-select-with-highlight pattern -
        // demoed here only, neither app retrofitted to use it yet). Window
        // at a fixed screen position so cmd_checkboxcontent/
        // cmd_radiocontent/cmd_progresscontent/cmd_slidercontent/
        // cmd_listcontent (shell.c) can read back known pixels, same
        // technique buttoncontent already uses for trigger 14.
        window w;
        bool created_w = window_create(&w, 400, 340, 200, 220, 0x00444444, 0x00222222);
        do_syscall(1, (u64) "window_create(w) ok=0x", (u64) created_w, 0);

        label lbl;
        label_init(&lbl, w.id, 10, 10, "hello world 123", 0x00FFFFFF, 0x00444444);

        checkbox cb;
        checkbox_init(&cb, w.id, 10, 40, 16, 0x00888888, 0x0000FF00, 0x00444444, false);

        // RadioButton group: radio0 starts selected (matches a real form's
        // "one option pre-selected" convention), radio1 starts unselected.
        int radio_selected = 0;
        radio_button radio0, radio1;
        radio_button_init(&radio0, w.id, 10, 70, 14, 0x00888888, 0x0000FF00, 0x00444444, 0, &radio_selected);
        radio_button_init(&radio1, w.id, 40, 70, 14, 0x00888888, 0x0000FF00, 0x00444444, 1, &radio_selected);

        progress_bar pbar;
        progress_bar_init(&pbar, w.id, 10, 100, 150, 12, 0x00222222, 0x0000AAFF, 30);

        slider sld;
        slider_init(&sld, w.id, 10, 120, 150, 12, 0x00222222, 0x0000FF00, 0, 100, 50);

        list_view lst;
        list_view_init(&lst, w.id, 10, 140, 150, 14, 0x00101010, 0x00405070, 0x00FFFFFF, 0x00444444);
        char* fruit_labels[3] = {"apple", "banana", "cherry"};
        list_view_set_rows(&lst, fruit_labels, 3);

        // Polls forever, same shape as every real interactive GUI app in
        // this codebase (desktop_shell.c/settings.c/service_manager.c's
        // own for(;;) toolbars) - NOT a fixed small-count loop, which
        // completes almost instantly under QEMU/TCG and leaves no real
        // wall-clock window to click during (memory documents trigger
        // 14/ring3button's own positive click path was never actually
        // verified for exactly this reason). Only prints on a real
        // click, not every poll, so the log stays readable across a
        // long real-world test.
        for (;;) {
            if (checkbox_poll(&cb)) {
                do_syscall(1, (u64) "checkbox_poll() clicked, checked=0x", (u64) cb.checked, 0);
            }
            int prev_selected = radio_selected;
            if (radio_button_poll(&radio0) || radio_button_poll(&radio1)) {
                // A real click already redrew whichever radio was clicked
                // (sets its own dot_color fill) - the toolkit's own stated
                // convention is the app redraws the REST of the group,
                // since there is no observer/event system here.
                if (prev_selected != radio_selected) {
                    if (prev_selected == 0) {
                        radio_button_draw(&radio0);
                    } else if (prev_selected == 1) {
                        radio_button_draw(&radio1);
                    }
                }
                do_syscall(1, (u64) "radio clicked, selected=0x", (u64) radio_selected, 0);
                // A real, hand-picked visible effect tying two widgets
                // together: selecting radio1 fills the progress bar to
                // 80%, radio0 back to 30% - proves progress_bar_set_percent
                // genuinely redraws (real value, not a one-shot init only).
                progress_bar_set_percent(&pbar, radio_selected == 1 ? 80 : 30);
            }
            if (slider_poll(&sld)) {
                do_syscall(1, (u64) "slider_poll() value=0x", (u64) sld.value, 0);
            }
            int clicked_row = list_view_poll(&lst);
            if (clicked_row >= 0) {
                do_syscall(1, (u64) "list_view_poll() clicked_row=0x", (u64) clicked_row, 0);
            }
        }
    } else if (trigger_value == 22) {
        // trigger 22 (ring3focus): real window focus + keyboard-to-window
        // routing (kernel/gfx/window/window.h/.c, syscalls 69/70). A
        // SEPARATE window/trigger from ring3widgets (21) - deliberately:
        // grabbing focus here would divert the keyboard away from the
        // console shell, breaking every *content command (checkboxcontent/
        // radiocontent/etc) trigger 21's own verification relies on typing
        // via sendkey while that window polls in the background.
        window fw;
        bool created_fw = window_create(&fw, 400, 60, 300, 100, 0x00303030, 0x00222222);
        do_syscall(1, (u64) "window_create(fw) ok=0x", (u64) created_fw, 0);

        bool focused_ok = gt_focus_window(fw.id);
        do_syscall(1, (u64) "gt_focus_window(fw) ok=0x", (u64) focused_ok, 0);

        label prompt_label;
        label_init(&prompt_label, fw.id, 10, 10, "type here:", 0x00FFFFFF, 0x00303030);

        // Real TextBox widget (gui_toolkit.h) - the ONE consumer of
        // gt_read_key in this trigger. It used to be a raw manual
        // gt_read_key-into-label echo (this milestone's own predecessor
        // proof, before text_box existed); that's replaced now rather
        // than left running alongside the real widget - both would drain
        // the SAME shared per-window keystroke queue, splitting keys
        // unpredictably between them (see gui_toolkit.h's own stated
        // "one shared queue per focused window" limitation).
        text_box tb;
        text_box_init(&tb, fw.id, 10, 40, 200, 18, 0x00000000, 0x00FFFFFF, 0x00888888, "");

        // Polls forever, same shape every real interactive GUI app here
        // uses - real keys arrive whenever the console shell's own
        // keyboard IRQ handler sees g_focused_window_id == fw.id.
        for (;;) {
            if (text_box_poll(&tb)) {
                do_syscall(1, (u64) "text_box_poll() length=0x", (u64) tb.length, 0);
            }
        }
    } else if (trigger_value == 36) {
        // trigger 36 (ring3inputevents): real input event queue (Faza II
        // point 17) - a SEPARATE window/trigger from ring3focus (22)/
        // ring3widgets (21), same reasoning as those: grabbing focus here
        // would divert the keyboard away from whatever else needs it.
        // Polls gt_read_event() (syscall 100) and prints every field of
        // every KEY_DOWN/MOUSE_BUTTON/MOUSE_WHEEL event it receives - a
        // real, decisive per-field check (scancode/modifiers/button id/
        // wheel sign), not just "an event arrived."
        window ie;
        bool created_ie = window_create(&ie, 450, 200, 250, 100, 0x00303030, 0x00222222);
        do_syscall(1, (u64) "window_create(ie) ok=0x", (u64) created_ie, 0);

        bool focused_ie = gt_focus_window(ie.id);
        do_syscall(1, (u64) "gt_focus_window(ie) ok=0x", (u64) focused_ie, 0);

        for (;;) {
            gt_input_event_t evt;
            if (gt_read_event(ie.id, &evt)) {
                if (evt.type == GT_INPUT_EVENT_KEY_DOWN) {
                    do_syscall(1, (u64) "KEY_DOWN scancode=0x", (u64) evt.scancode, 0);
                    do_syscall(1, (u64) "KEY_DOWN modifiers=0x", (u64) evt.modifiers, 0);
                    do_syscall(1, (u64) "KEY_DOWN ascii=0x", (u64) evt.ascii, 0);
                } else if (evt.type == GT_INPUT_EVENT_MOUSE_BUTTON) {
                    do_syscall(1, (u64) "MOUSE_BUTTON button=0x", (u64) evt.button, 0);
                    do_syscall(1, (u64) "MOUSE_BUTTON pressed=0x", (u64) evt.pressed, 0);
                } else if (evt.type == GT_INPUT_EVENT_MOUSE_WHEEL) {
                    do_syscall(1, (u64) "MOUSE_WHEEL delta=0x", (u64) evt.wheel_delta, 0);
                }
            }
        }
    } else {
        return false;
    }
    return true;
}
