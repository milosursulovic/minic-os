#include "gui.h"
#include "../../../kernel/drivers/io/io.h"
#include "../../../kernel/lib/strings.h"
#include "../../../kernel/drivers/vbe/vbe.h"
#include "../../../kernel/drivers/mouse/mouse.h"
#include "../../../kernel/gfx/window/window.h"
#include "../../../kernel/gfx/image/image.h"
#include "../../../kernel/gfx/png/png.h"

// QEMU/Bochs's ACPI PM shutdown trick: writing 0x2000 to the PM1a control
// port (0x604 under QEMU's default i440fx machine) requests S5 (soft off)
// - no real ACPI table parsing exists in this kernel, this is a
// QEMU/Bochs-specific shortcut, not real-hardware ACPI. Never returns on
// QEMU; on real hardware (or a different virtual chipset) it's a no-op.
// Closes the GUI Terminal window (proc/apps/terminal/terminal.c
// registers its own window id via syscall 58 the moment it creates it -
// see kernel/gfx/window/window.h's g_terminal_window_id) - a real "exit"
// like typing exit in a real terminal closes it, not a console-shell
// exit (there's nowhere for the console itself to exit to).
void cmd_exit(void) {
    if (g_terminal_window_id < 0) {
        vga_print("exit: no terminal window open");
        serial_print("exit: no terminal window open");
        return;
    }
    window_close(g_terminal_window_id);
    g_terminal_window_id = -1;
    vga_print("closed terminal window");
    serial_print("closed terminal window");
}

// Reads back a pixel inside ring3widgets's checkbox inner fill (see
// ring3prog.c trigger 21). Window w is at screen (400,340), body starts
// at (400,360); checkbox drawn at body-local (10,40) size 16, 2px inset
// -> inner fill screen (412,402)-(423,413). (413,404) - near the top-left
// corner of that range, away from where the cursor tip typically rests
// after a click landed on this box from above - reads 0x00444444
// (unchecked, bg_color) or 0x0000FF00 (checked, check_color), live and
// controllable via QEMU monitor mouse_move/mouse_button, same technique
// cmd_buttoncontent already uses.
void cmd_checkboxcontent(void) {
    vga_print("checkboxcontent fill=0x");
    serial_print("checkboxcontent fill=0x");
    print_hex((u64) fb_get_pixel(413, 404));
}

// Reads back a pixel inside each of ring3widgets's two radio buttons (see
// ring3prog.c trigger 21). radio0 at body-local (10,70) size 14 -> screen
// (410,430), inner fill (412,432)-(421,441); radio1 at body-local (40,70)
// -> screen (440,430), inner fill (442,432)-(451,441). (413,433)/(443,433)
// - near the top-left corner of each inner range, same "away from the
// cursor's own resting point" reasoning cmd_checkboxcontent already
// documents - read 0x00444444 (unselected, bg_color) or 0x0000FF00
// (selected, dot_color).
void cmd_radiocontent(void) {
    vga_print("radiocontent r0=0x");
    serial_print("radiocontent r0=0x");
    print_hex((u64) fb_get_pixel(413, 433));
    vga_print(" r1=0x");
    serial_print(" r1=0x");
    print_hex((u64) fb_get_pixel(443, 433));
}

// Reads back two pixels inside ring3widgets's progress bar (body-local
// (10,100) size 150x12 -> screen (410,460)-(560,472)). x=430 sits inside
// the bar's first 30% (410-455) - always fill_color once the demo's
// initial progress_bar_init(..., 30) has run. x=500 sits inside 80%
// (410-530) but past 30% - fill_color only after selecting radio1 sets
// it to 80% (see ring3prog.c trigger 21) - reading both proves the fill
// width genuinely scales with a real progress_bar_set_percent() call,
// not just an on/off flag.
void cmd_progresscontent(void) {
    vga_print("progresscontent x30pct=0x");
    serial_print("progresscontent x30pct=0x");
    print_hex((u64) fb_get_pixel(430, 466));
    vga_print(" x80pct=0x");
    serial_print(" x80pct=0x");
    print_hex((u64) fb_get_pixel(500, 466));
}

// Reads back the slider's default handle position (body-local (10,120)
// size 150x12, track screen (410,480)-(560,492)). With min=0/max=100/
// initial=50 (see ring3prog.c trigger 21), usable_width=150-6=144,
// handle_offset=50*144/100=72 -> handle screen x=482-488. (485,486) sits
// inside that range - reads 0x0000ff00 (handle_color) by default. After
// a real drag this same point may or may not still show the handle
// (expected - the point is only a fixed baseline check); verify a real
// drag via screendump instead (keyboard dies after the first
// mouse_button of that boot anyway, so a second sendkey-based readback
// isn't possible in the same test run regardless).
void cmd_slidercontent(void) {
    vga_print("slidercontent handle_at_default=0x");
    serial_print("slidercontent handle_at_default=0x");
    print_hex((u64) fb_get_pixel(485, 486));
}

// Reads back one pixel inside each of ring3widgets's 3 list rows
// (body-local (10,140) width 150 row_height 14 -> screen (410,500)-
// (560,542); row0 y500-514, row1 y514-528, row2 y528-542). x=550 - well
// past any real label's text width (labels here are 5-6 chars, ~36px)
// so this always reads the row's flat background fill, never a glyph's
// foreground pixel. Before any click all three read 0x00101010
// (row_color, none selected); after a real click on a row, that row's
// sample point reads 0x00405070 (selected_color) and the others stay
// row_color.
void cmd_listcontent(void) {
    vga_print("listcontent row0=0x");
    serial_print("listcontent row0=0x");
    print_hex((u64) fb_get_pixel(550, 505));
    vga_print(" row1=0x");
    serial_print(" row1=0x");
    print_hex((u64) fb_get_pixel(550, 519));
    vga_print(" row2=0x");
    serial_print(" row2=0x");
    print_hex((u64) fb_get_pixel(550, 533));
}

// Sets an 800x600x32 linear framebuffer, draws a background fill plus a
// contrasting rect, then reads pixels back (not just the values we sent) to
// prove real hardware round trips, including exact rect-boundary precision.
void cmd_fb(void) {
    bool ok = vbe_init(800, 600);
    if (!ok) {
        vga_print("framebuffer init failed - no Bochs VBE VGA device found");
        serial_print("framebuffer init failed - no Bochs VBE VGA device found");
        return;
    }

    vga_print("fb lfb_phys=0x");
    serial_print("fb lfb_phys=0x");
    print_hex((u64) vbe_lfb_phys());
    vga_print(" vaddr=0x");
    serial_print(" vaddr=0x");
    print_hex(g_fb_vaddr);
    vga_print(" xres=0x");
    serial_print(" xres=0x");
    print_hex((u64) vbe_read_reg(1));
    vga_print(" yres=0x");
    serial_print(" yres=0x");
    print_hex((u64) vbe_read_reg(2));
    vga_print(" bpp=0x");
    serial_print(" bpp=0x");
    print_hex((u64) vbe_read_reg(3));
    vga_print(" pitch=0x");
    serial_print(" pitch=0x");
    print_hex((u64) g_fb_pitch);

    fb_fill_rect(0, 0, 800, 600, 0x00001133);
    fb_fill_rect(100, 100, 200, 150, 0x00FF0000);

    vga_print(" bg=0x");
    serial_print(" bg=0x");
    print_hex((u64) fb_get_pixel(0, 0));
    vga_print(" just_outside=0x");
    serial_print(" just_outside=0x");
    print_hex((u64) fb_get_pixel(99, 100));
    vga_print(" rect_topleft=0x");
    serial_print(" rect_topleft=0x");
    print_hex((u64) fb_get_pixel(100, 100));
    vga_print(" rect_bottomright=0x");
    serial_print(" rect_bottomright=0x");
    print_hex((u64) fb_get_pixel(299, 249));
    vga_print(" past_rect=0x");
    serial_print(" past_rect=0x");
    print_hex((u64) fb_get_pixel(300, 250));
}

// Draws "HI" straight onto the raw framebuffer with fb_draw_string, then
// reads back specific hand-computed pixels: a foreground stroke in each of
// the two glyphs, an "off" pixel inside a glyph's own cell, the untouched
// inter-character gap column, and a pixel well past both characters - same
// discipline as cmd_fb's rect read-backs.
void cmd_text(void) {
    bool ok = vbe_init(800, 600);
    if (!ok) {
        vga_print("framebuffer init failed - no Bochs VBE VGA device found");
        serial_print("framebuffer init failed - no Bochs VBE VGA device found");
        return;
    }

    fb_fill_rect(0, 0, 800, 600, 0x00001133);
    fb_draw_string(100, 100, "HI", 0x00FFFFFF, 0x00001133);

    // 'H' bitmap row0=10001 (col0 on), row1=10001 (col1 off), row3=11111
    // (col2 on, the crossbar). 'I' starts at x+6, row0=11111 (col2 on).
    vga_print("text h_stroke=0x");
    serial_print("text h_stroke=0x");
    print_hex((u64) fb_get_pixel(100, 100));
    vga_print(" h_gap=0x");
    serial_print(" h_gap=0x");
    print_hex((u64) fb_get_pixel(101, 101));
    vga_print(" h_crossbar=0x");
    serial_print(" h_crossbar=0x");
    print_hex((u64) fb_get_pixel(102, 103));
    vga_print(" between_chars=0x");
    serial_print(" between_chars=0x");
    print_hex((u64) fb_get_pixel(105, 100));
    vga_print(" i_stroke=0x");
    serial_print(" i_stroke=0x");
    print_hex((u64) fb_get_pixel(108, 100));
    vga_print(" past_text=0x");
    serial_print(" past_text=0x");
    print_hex((u64) fb_get_pixel(200, 100));
}

// Enables the PS/2 mouse and reports its currently tracked state. Safe to
// re-run - init doesn't reset position/buttons/packet count, so running
// this once, injecting real input, then running it again proves a genuine
// hardware round trip rather than just "the driver didn't crash".
void cmd_mouse(void) {
    mouse_init();
    vga_print("mouse x=0x");
    serial_print("mouse x=0x");
    print_hex((u64) g_mouse_x);
    vga_print(" y=0x");
    serial_print(" y=0x");
    print_hex((u64) g_mouse_y);
    vga_print(" buttons=0x");
    serial_print(" buttons=0x");
    print_hex((u64) g_mouse_buttons);
    vga_print(" packets=0x");
    serial_print(" packets=0x");
    print_hex((u64) g_mouse_packet_count);
    vga_print(" rawbytes=0x");
    serial_print(" rawbytes=0x");
    print_hex((u64) g_mouse_raw_byte_count);
}

// A real window table + Z-order compositor demo: 2 overlapping windows plus
// a 3rd standalone one, then a raise, a move, and a close - each step
// checked by reading the actual composited pixels back, not just trusting
// the calls succeeded.
void cmd_win(void) {
    bool ok = vbe_init(800, 600);
    if (!ok) {
        vga_print("window server init failed - no framebuffer");
        serial_print("window server init failed - no framebuffer");
        return;
    }

    int a = window_create(50, 50, 200, 150, 0x00FF0000, 0x00800000);
    int b = window_create(150, 120, 200, 150, 0x0000FF00, 0x00008000);
    int c = window_create(500, 400, 150, 100, 0x000000FF, 0x00000080);
    compositor_redraw();

    vga_print("win a=0x");
    serial_print("win a=0x");
    print_hex((u64) a);
    vga_print(" b=0x");
    serial_print(" b=0x");
    print_hex((u64) b);
    vga_print(" c=0x");
    serial_print(" c=0x");
    print_hex((u64) c);

    vga_print(" overlap_b_on_top=0x");
    serial_print(" overlap_b_on_top=0x");
    print_hex((u64) fb_get_pixel(200, 150));
    vga_print(" a_only=0x");
    serial_print(" a_only=0x");
    print_hex((u64) fb_get_pixel(60, 180));
    vga_print(" b_only=0x");
    serial_print(" b_only=0x");
    print_hex((u64) fb_get_pixel(300, 150));
    vga_print(" c_body=0x");
    serial_print(" c_body=0x");
    print_hex((u64) fb_get_pixel(550, 450));
    vga_print(" bg=0x");
    serial_print(" bg=0x");
    print_hex((u64) fb_get_pixel(400, 300));

    window_raise(a);
    compositor_redraw();
    vga_print(" overlap_a_raised=0x");
    serial_print(" overlap_a_raised=0x");
    print_hex((u64) fb_get_pixel(200, 150));

    window_move(c, 550, 420);
    compositor_redraw();
    vga_print(" c_old_after_move=0x");
    serial_print(" c_old_after_move=0x");
    print_hex((u64) fb_get_pixel(510, 450));
    vga_print(" c_new_after_move=0x");
    serial_print(" c_new_after_move=0x");
    print_hex((u64) fb_get_pixel(670, 470));

    window_fill_content_rect(c, 0, 0, 150, 80, 0x00333333);
    window_fill_content_rect(c, 20, 20, 50, 50, 0x00FFFF00);
    compositor_redraw();
    vga_print(" c_content_base=0x");
    serial_print(" c_content_base=0x");
    print_hex((u64) fb_get_pixel(650, 450));
    vga_print(" c_content_accent=0x");
    serial_print(" c_content_accent=0x");
    print_hex((u64) fb_get_pixel(590, 480));

    window_close(b);
    compositor_redraw();
    vga_print(" b_gone=0x");
    serial_print(" b_gone=0x");
    print_hex((u64) fb_get_pixel(300, 150));
    vga_print(" a_survives=0x");
    serial_print(" a_survives=0x");
    print_hex((u64) fb_get_pixel(200, 150));
    vga_print(" windows_left=0x");
    serial_print(" windows_left=0x");
    print_hex((u64) g_window_zorder_count);
}

// Dumps the real window table's current z-order (bottom to top) - id,
// position, size. Same introspection discipline as netconns/objs.
void cmd_winlist(void) {
    vga_print("windows: 0x");
    serial_print("windows: 0x");
    print_hex((u64) g_window_zorder_count);
    int i = 0;
    while (i < g_window_zorder_count) {
        window* w = &g_windows[g_window_zorder[i]];
        vga_print(" id=0x");
        serial_print(" id=0x");
        print_hex((u64) g_window_zorder[i]);
        vga_print(" x=0x");
        serial_print(" x=0x");
        print_hex((u64) w->x);
        vga_print(" y=0x");
        serial_print(" y=0x");
        print_hex((u64) w->y);
        vga_print(" w=0x");
        serial_print(" w=0x");
        print_hex((u64) w->width);
        vga_print(" h=0x");
        serial_print(" h=0x");
        print_hex((u64) w->height);
        vga_print(" content=0x");
        serial_print(" content=0x");
        print_hex((u64) w->has_content);
        i = i + 1;
    }
}

// Reads back the two pixels ring3win's window d (see ring3prog.c trigger 11)
// should have drawn via window_fill_rect - run this after ring3win.
void cmd_wincontent(void) {
    vga_print("wincontent base=0x");
    serial_print("wincontent base=0x");
    print_hex((u64) fb_get_pixel(150, 420));
    vga_print(" accent=0x");
    serial_print(" accent=0x");
    print_hex((u64) fb_get_pixel(90, 360));
}

// Reads back pixels ring3text's window e (see ring3prog.c trigger 13) should
// have drawn via window_draw_text - run this after ring3text. Window e is at
// screen (300,300), titlebar 20px, so its body starts at (300,320); text was
// drawn at body-local (10,10) -> screen (310,330). Only checking pixels the
// glyphs explicitly set 'on' (not gap/background pixels, whose value would
// depend on whatever this window slot's content buffer held before - same
// restraint cmd_wincontent already applies to window d).
void cmd_textcontent(void) {
    // 'O' row0=01110 (offset1 on), row1=10001 (offset0 on, left stroke).
    vga_print("textcontent o_top=0x");
    serial_print("textcontent o_top=0x");
    print_hex((u64) fb_get_pixel(311, 330));
    vga_print(" o_left_stroke=0x");
    serial_print(" o_left_stroke=0x");
    print_hex((u64) fb_get_pixel(310, 331));
    // 'S' starts at body-local x=16 (screen x=300+16=316), row0=01111 (offset1 on).
    vga_print(" s_top=0x");
    serial_print(" s_top=0x");
    print_hex((u64) fb_get_pixel(317, 330));
}

// Reads back a pixel inside ring3button's button fill (see ring3prog.c
// trigger 14) - run this after/while ring3button is polling. Window f is
// at screen (400,200), body starts at (400,220); button drawn at
// body-local (20,30) size 100x30 -> screen (420,250)..(519,279). Pixel
// picked well past the 2-char "OK" label so it reads the flat fill color:
// 0x00888888 (normal) or 0x0000FF00 (pressed, cursor over it + left down),
// live and controllable via QEMU monitor mouse_move/mouse_button.
void cmd_buttoncontent(void) {
    vga_print("buttoncontent fill=0x");
    serial_print("buttoncontent fill=0x");
    print_hex((u64) fb_get_pixel(470, 260));
}

// Reads back pixels from the desktop shell (proc/apps/desktop_shell.c),
// auto-spawned at boot - no trigger needed, unlike ring3button/ring3text.
// wallpaper: a point clear of both the taskbar AND the terminal window
// (proc/apps/terminal.c, at (100,60)-(600,380) - (400,100) used to be a valid
// open-wallpaper point before that window existed and started covering
// it), must be the flat wallpaper color. taskbar_top/taskbar_bottom: the
// same x, first and last row of the 20px-tall taskbar - both must read
// the taskbar's own explicit background fill, proving no titlebar strip
// got drawn at the top (a titlebar bug would leave taskbar_top black -
// the window's untouched-cell default - while taskbar_bottom stayed
// correct).
void cmd_desktop(void) {
    vga_print("desktop wallpaper=0x");
    serial_print("desktop wallpaper=0x");
    print_hex((u64) fb_get_pixel(700, 450));
    vga_print(" taskbar_top=0x");
    serial_print(" taskbar_top=0x");
    print_hex((u64) fb_get_pixel(400, 580));
    vga_print(" taskbar_bottom=0x");
    serial_print(" taskbar_bottom=0x");
    print_hex((u64) fb_get_pixel(400, 599));
    vga_print(" label_m_left=0x");
    serial_print(" label_m_left=0x");
    print_hex((u64) fb_get_pixel(8, 586));
    vga_print(" label_m_row1=0x");
    serial_print(" label_m_row1=0x");
    print_hex((u64) fb_get_pixel(8, 587));
}

#pragma GCC visibility push(hidden)
extern u8 g_png_test_stored_start;
extern u8 g_png_test_stored_end;
extern u8 g_png_test_huffman_start;
extern u8 g_png_test_huffman_end;
extern u8 g_cursor_png_start;
extern u8 g_cursor_png_end;
#pragma GCC visibility pop

static void print_labeled_hex(const char* label, u64 value) {
    vga_print(label);
    serial_print(label);
    print_hex(value);
}

// Exercises the hand-written PNG decoder (kernel/gfx/png/png.c) against
// three real embedded PNGs and prints exact values to compare by hand:
// a tiny stored-DEFLATE-block PNG with hand-picked pixels, a 64x64
// dynamic-Huffman PNG whose pixels follow a known formula, and the real
// cursor.png asset - plus a deliberate one-byte corruption to prove the
// CRC32 check actually rejects bad input instead of decoding garbage.
void cmd_pngtest(void) {
    u32 stored_size = (u32) ((u64) &g_png_test_stored_end - (u64) &g_png_test_stored_start);
    image stored_img;
    bool ok1 = png_decode(&g_png_test_stored_start, stored_size, &stored_img);
    print_labeled_hex("stored ok=0x", ok1 ? 1 : 0);
    if (ok1) {
        print_labeled_hex(" w=0x", stored_img.width);
        print_labeled_hex(" h=0x", stored_img.height);
        print_labeled_hex(" px0=0x", stored_img.pixels[0]);   // expect 0x00000000 (0,0,0)
        print_labeled_hex(" px1=0x", stored_img.pixels[1]);   // expect 0x00FF0000 (255,0,0)
        print_labeled_hex(" px15=0x", stored_img.pixels[15]); // expect 0x00FFFFFF (255,255,255)
    }

    u32 huff_size = (u32) ((u64) &g_png_test_huffman_end - (u64) &g_png_test_huffman_start);
    image huff_img;
    bool ok2 = png_decode(&g_png_test_huffman_start, huff_size, &huff_img);
    print_labeled_hex(" huffman ok=0x", ok2 ? 1 : 0);
    if (ok2) {
        print_labeled_hex(" w=0x", huff_img.width);
        print_labeled_hex(" h=0x", huff_img.height);
        u32 x = 37;
        u32 y = 50;
        u32 expect = (((x * 7) % 256) << 16) | (((y * 11) % 256) << 8) | (((x ^ y) * 3) % 256);
        print_labeled_hex(" px37_50=0x", huff_img.pixels[y * huff_img.width + x]);
        print_labeled_hex(" expect=0x", expect);
    }

    u32 cursor_size = (u32) ((u64) &g_cursor_png_end - (u64) &g_cursor_png_start);
    image cursor_img;
    bool ok3 = png_decode(&g_cursor_png_start, cursor_size, &cursor_img);
    print_labeled_hex(" cursor ok=0x", ok3 ? 1 : 0);
    if (ok3) {
        print_labeled_hex(" w=0x", cursor_img.width);
        print_labeled_hex(" h=0x", cursor_img.height);
        print_labeled_hex(" px0=0x", cursor_img.pixels[0]);           // expect 0x00000000 (black outline)
        print_labeled_hex(" px_transparent=0x", cursor_img.pixels[1]); // expect 0xFFFFFFFF
    }

    // Corrupt one byte inside the stored PNG's chunk data (CRC32 covers
    // the whole type+data span of every chunk, so any single-byte flip
    // past the 8-byte signature breaks some chunk's CRC) and confirm
    // png_decode rejects it instead of producing wrong pixels.
    if (stored_size <= 256) {
        u8 corrupt[256];
        for (u32 i = 0; i < stored_size; i = i + 1) {
            corrupt[i] = (&g_png_test_stored_start)[i];
        }
        corrupt[stored_size / 2] = corrupt[stored_size / 2] ^ 0xFF;
        image bad_img;
        bool ok4 = png_decode(corrupt, stored_size, &bad_img);
        print_labeled_hex(" corrupt_rejected=0x", ok4 ? 0 : 1); // expect 1
    }
}
