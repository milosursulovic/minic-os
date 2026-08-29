#pragma once

#include "../../../types.h"

#pragma GCC visibility push(hidden)

// Reads the current wall-clock time from the standard PC CMOS RTC (index/
// data ports 0x70/0x71) - real hardware, and QEMU emulates it by default.
// Always returns 24-hour values regardless of what mode the RTC itself is
// configured in (BCD/binary, 12/24-hour) - see rtc.c for the conversion.
void rtc_read_time(u8* hour, u8* minute, u8* second);

// Same CMOS RTC, its date registers - no century register read (not at a
// guaranteed-standard index across all real hardware, only conventional),
// so `year` is always `2000 + <RTC's 2-digit year>` - a deliberate, honest
// simplification, not correct past 2099.
void rtc_read_date(u8* day, u8* month, u16* year);

#pragma GCC visibility pop
