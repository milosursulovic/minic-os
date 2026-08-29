# Embeds assets/cursor.png directly into kernel.elf - same .incbin-between-
# two-global-labels convention as proc/demo/ring3prog/ring3blob.s.
.intel_syntax noprefix

.global g_cursor_png_start
.global g_cursor_png_end

g_cursor_png_start:
.incbin "../../../assets/cursor.png"
g_cursor_png_end:
