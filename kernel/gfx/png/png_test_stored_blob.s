# Embeds assets/png_test_stored.png (a tiny, compress_level=0 PNG - forces
# a DEFLATE stored block) for the decoder's own self-test shell command.
.intel_syntax noprefix

.global g_png_test_stored_start
.global g_png_test_stored_end

g_png_test_stored_start:
.incbin "../../../assets/png_test_stored.png"
g_png_test_stored_end:
