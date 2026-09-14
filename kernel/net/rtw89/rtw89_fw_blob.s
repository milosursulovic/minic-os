# Embeds assets/rtw8852b_fw.bin directly into kernel.elf - same .incbin-
# between-two-global-labels convention as kernel/gfx/png/wallpaper_blob.s.
# This is the one, narrow, documented exception to this project's
# hand-written-only rule (CLAUDE.md, 2026-09-14) - real Realtek vendor
# firmware for the RTL8852BE WiFi chip, uploaded to the device verbatim
# as inert data. It never executes on this kernel's own CPU - only on
# the WiFi chip's own embedded core, via kernel/net/rtw89/rtw89.c's own,
# 100% hand-written upload protocol.
.intel_syntax noprefix

.global g_rtw89_fw_start
.global g_rtw89_fw_end

g_rtw89_fw_start:
.incbin "../../../assets/rtw8852b_fw.bin"
g_rtw89_fw_end:
