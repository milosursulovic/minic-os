// Standard PC CMOS real-time clock - index/data port pair (0x70/0x71),
// same chip every PC (and QEMU by default) has had since the original AT.

#include "rtc.h"
#include "../io/io.h"

#define CMOS_INDEX_PORT 0x70
#define CMOS_DATA_PORT 0x71
#define CMOS_REG_SECONDS 0x00
#define CMOS_REG_MINUTES 0x02
#define CMOS_REG_HOURS 0x04
#define CMOS_REG_DAY 0x07
#define CMOS_REG_MONTH 0x08
#define CMOS_REG_YEAR 0x09
#define CMOS_REG_STATUS_A 0x0A
#define CMOS_REG_STATUS_B 0x0B
#define STATUS_A_UPDATE_IN_PROGRESS 0x80
#define STATUS_B_BINARY_MODE 0x04
#define STATUS_B_24_HOUR_MODE 0x02
#define HOUR_PM_BIT 0x80

static u8 cmos_read(u8 reg) {
    outb(CMOS_INDEX_PORT, reg);
    return inb(CMOS_DATA_PORT);
}

// Save/restore IF (not a bare cli/sti pair, so this is correct regardless
// of whether interrupts were already off in the caller) - same private
// pattern kernel/gfx/window/window.c already uses for its own critical
// section, not a shared kernel-wide utility. Needed here because the
// index-select outb() and the following inb() are two separate
// instructions - the timer ISR preempting between them (or another task's
// own CMOS read interleaving) would read the wrong register.
static u64 disable_interrupts(void) {
    u64 saved_flags;
    __asm__ volatile("pushfq\n\tpop %0\n\tcli" : "=r"(saved_flags) : : "memory");
    return saved_flags;
}

static void restore_interrupts(u64 saved_flags) {
    __asm__ volatile("push %0\n\tpopfq" : : "r"(saved_flags) : "memory", "cc");
}

static u8 bcd_to_binary(u8 value) {
    return (u8) (((value >> 4) * 10) + (value & 0x0F));
}

void rtc_read_time(u8* hour, u8* minute, u8* second) {
    // Reading mid-update returns garbage - wait for the Update-In-Progress
    // bit to clear first. Bounded, not an infinite poll, in case something
    // is genuinely wrong with the RTC.
    int wait_iterations = 0;
    while ((cmos_read(CMOS_REG_STATUS_A) & STATUS_A_UPDATE_IN_PROGRESS) != 0
           && wait_iterations < 100000) {
        wait_iterations = wait_iterations + 1;
    }

    u64 saved_flags = disable_interrupts();
    u8 raw_seconds = cmos_read(CMOS_REG_SECONDS);
    u8 raw_minutes = cmos_read(CMOS_REG_MINUTES);
    u8 raw_hours = cmos_read(CMOS_REG_HOURS);
    u8 status_b = cmos_read(CMOS_REG_STATUS_B);
    restore_interrupts(saved_flags);

    bool is_pm = (raw_hours & HOUR_PM_BIT) != 0;
    u8 hour_value = (u8) (raw_hours & ~HOUR_PM_BIT);
    u8 minute_value = raw_minutes;
    u8 second_value = raw_seconds;

    if ((status_b & STATUS_B_BINARY_MODE) == 0) {  // BCD mode
        hour_value = bcd_to_binary(hour_value);
        minute_value = bcd_to_binary(minute_value);
        second_value = bcd_to_binary(second_value);
    }

    if ((status_b & STATUS_B_24_HOUR_MODE) == 0) {  // 12-hour mode - convert to 24-hour
        if (is_pm && hour_value != 12) {
            hour_value = (u8) (hour_value + 12);
        } else if (!is_pm && hour_value == 12) {
            hour_value = 0;
        }
    }

    *hour = hour_value;
    *minute = minute_value;
    *second = second_value;
}

void rtc_read_date(u8* day, u8* month, u16* year) {
    int wait_iterations = 0;
    while ((cmos_read(CMOS_REG_STATUS_A) & STATUS_A_UPDATE_IN_PROGRESS) != 0
           && wait_iterations < 100000) {
        wait_iterations = wait_iterations + 1;
    }

    u64 saved_flags = disable_interrupts();
    u8 raw_day = cmos_read(CMOS_REG_DAY);
    u8 raw_month = cmos_read(CMOS_REG_MONTH);
    u8 raw_year = cmos_read(CMOS_REG_YEAR);
    u8 status_b = cmos_read(CMOS_REG_STATUS_B);
    restore_interrupts(saved_flags);

    u8 day_value = raw_day;
    u8 month_value = raw_month;
    u8 year_value = raw_year;

    if ((status_b & STATUS_B_BINARY_MODE) == 0) {  // BCD mode
        day_value = bcd_to_binary(day_value);
        month_value = bcd_to_binary(month_value);
        year_value = bcd_to_binary(year_value);
    }

    *day = day_value;
    *month = month_value;
    *year = (u16) (2000 + year_value);  // no century register read - see rtc.h
}
