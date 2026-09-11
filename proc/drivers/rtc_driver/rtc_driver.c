// Real ring3 driver isolation proof of concept (Faza I point 14, item
// 14): the CMOS RTC's actual port I/O now happens from HERE, in ring3,
// through the capability-gated syscall 99 (proc/gui_toolkit/port_io.h) -
// not from kernel code. Reimplements kernel/drivers/rtc/rtc.c's own
// BCD/12-24h conversion logic verbatim; that kernel-side copy stays
// exactly as it is, used only for the kernel's own tiny, early-boot,
// unavoidable need (kernel/lib/rand.c's ASLR seed, which runs before any
// ring3 process could exist) - an explicit, documented exception, not a
// silent contradiction of "isolation".
//
// Fixed, documented handle layout - granted by kmain.c at spawn time,
// before any preemption is possible (same no-yield-yet boot window
// spawn_process()'s own handle-0-self wiring already relies on):
//   handle 0 = self (spawn_process()'s own default)
//   handle 1 = OBJ_IO_PORT_RANGE capability, ports 0x70-0x71 only
//   handle 2 = request channel (RIGHT_RECEIVE) - a 1-byte op code, 0=time/1=date
//   handle 3 = response channel (RIGHT_SEND) - an rtc_response struct
//
// _start must be at offset 0 - see ring3prog.c's own comment on this;
// same __attribute__((section(".text.start"))) + ring3.ld requirement.

#include "../../../types.h"
#include "../../gui_toolkit.h"

#define IO_HANDLE 1
#define REQUEST_CHANNEL_HANDLE 2
#define RESPONSE_CHANNEL_HANDLE 3

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

typedef struct {
    bool is_date;
    u8 v1;   // hour or day
    u8 v2;   // minute or month
    u16 v3;  // second or year
} rtc_response;

static u8 cmos_read(u8 reg) {
    gt_port_outb(IO_HANDLE, CMOS_INDEX_PORT, reg);
    return (u8) gt_port_inb(IO_HANDLE, CMOS_DATA_PORT);
}

static u8 bcd_to_binary(u8 value) {
    return (u8) (((value >> 4) * 10) + (value & 0x0F));
}

static void wait_for_update_complete(void) {
    int wait_iterations = 0;
    while ((cmos_read(CMOS_REG_STATUS_A) & STATUS_A_UPDATE_IN_PROGRESS) != 0
           && wait_iterations < 100000) {
        wait_iterations = wait_iterations + 1;
    }
}

static void read_time(u8* hour, u8* minute, u8* second) {
    wait_for_update_complete();
    u8 raw_seconds = cmos_read(CMOS_REG_SECONDS);
    u8 raw_minutes = cmos_read(CMOS_REG_MINUTES);
    u8 raw_hours = cmos_read(CMOS_REG_HOURS);
    u8 status_b = cmos_read(CMOS_REG_STATUS_B);

    bool is_pm = (raw_hours & HOUR_PM_BIT) != 0;
    u8 hour_value = (u8) (raw_hours & ~HOUR_PM_BIT);
    u8 minute_value = raw_minutes;
    u8 second_value = raw_seconds;

    if ((status_b & STATUS_B_BINARY_MODE) == 0) {
        hour_value = bcd_to_binary(hour_value);
        minute_value = bcd_to_binary(minute_value);
        second_value = bcd_to_binary(second_value);
    }

    if ((status_b & STATUS_B_24_HOUR_MODE) == 0) {
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

static void read_date(u8* day, u8* month, u16* year) {
    wait_for_update_complete();
    u8 raw_day = cmos_read(CMOS_REG_DAY);
    u8 raw_month = cmos_read(CMOS_REG_MONTH);
    u8 raw_year = cmos_read(CMOS_REG_YEAR);
    u8 status_b = cmos_read(CMOS_REG_STATUS_B);

    u8 day_value = raw_day;
    u8 month_value = raw_month;
    u8 year_value = raw_year;

    if ((status_b & STATUS_B_BINARY_MODE) == 0) {
        day_value = bcd_to_binary(day_value);
        month_value = bcd_to_binary(month_value);
        year_value = bcd_to_binary(year_value);
    }

    *day = day_value;
    *month = month_value;
    *year = (u16) (2000 + year_value);
}

__attribute__((section(".text.start")))
void _start(void) {
    for (;;) {
        u8 op;
        gt_channel_receive_msg(REQUEST_CHANNEL_HANDLE, &op, 1);

        rtc_response resp;
        if (op == 0) {
            u8 hour, minute, second;
            read_time(&hour, &minute, &second);
            resp.is_date = false;
            resp.v1 = hour;
            resp.v2 = minute;
            resp.v3 = second;
        } else {
            u8 day, month;
            u16 year;
            read_date(&day, &month, &year);
            resp.is_date = true;
            resp.v1 = day;
            resp.v2 = month;
            resp.v3 = year;
        }
        gt_channel_send_msg(RESPONSE_CHANNEL_HANDLE, &resp, sizeof(resp));
    }
}
