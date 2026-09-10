# Embeds assets/wallpaper.png directly into kernel.elf - same .incbin-
# between-two-global-labels convention as cursor_blob.s.
.intel_syntax noprefix

.global g_wallpaper_png_start
.global g_wallpaper_png_end

g_wallpaper_png_start:
.incbin "../../../assets/wallpaper.png"
g_wallpaper_png_end:
