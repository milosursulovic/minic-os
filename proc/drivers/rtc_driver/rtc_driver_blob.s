# Wraps the flattened rtc_driver.bin the same way hello_service_blob.s/
# init_blob.s wrap their own programs.
.intel_syntax noprefix

.global g_rtc_driver_prog_start
.global g_rtc_driver_prog_end

g_rtc_driver_prog_start:
.incbin "../../../build/proc/drivers/rtc_driver/rtc_driver.bin"
g_rtc_driver_prog_end:
