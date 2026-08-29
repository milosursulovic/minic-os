# Embeds assets/png_test_huffman.png (a 64x64 algorithmic pattern, real
# zlib compression - exercises dynamic-Huffman decoding) for the
# decoder's own self-test shell command.
.intel_syntax noprefix

.global g_png_test_huffman_start
.global g_png_test_huffman_end

g_png_test_huffman_start:
.incbin "../../../assets/png_test_huffman.png"
g_png_test_huffman_end:
