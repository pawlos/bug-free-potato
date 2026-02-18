#include "rtc.h"
#include "io.h"

static constexpr pt::uint16_t CMOS_ADDR = 0x70;
static constexpr pt::uint16_t CMOS_DATA = 0x71;

static constexpr pt::uint8_t REG_SECONDS  = 0x00;
static constexpr pt::uint8_t REG_MINUTES  = 0x02;
static constexpr pt::uint8_t REG_HOURS    = 0x04;
static constexpr pt::uint8_t REG_DAY      = 0x07;
static constexpr pt::uint8_t REG_MONTH    = 0x08;
static constexpr pt::uint8_t REG_YEAR     = 0x09;
static constexpr pt::uint8_t REG_STATUS_A = 0x0A;
static constexpr pt::uint8_t REG_STATUS_B = 0x0B;

static pt::uint8_t cmos_read(pt::uint8_t reg)
{
    IO::outb(CMOS_ADDR, reg);
    return IO::inb(CMOS_DATA);
}

static bool update_in_progress()
{
    return cmos_read(REG_STATUS_A) & 0x80;
}

static pt::uint8_t bcd_to_bin(pt::uint8_t bcd)
{
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

void rtc_read(RTCTime* out)
{
    // Wait until no update is in progress
    while (update_in_progress()) {}

    out->seconds = cmos_read(REG_SECONDS);
    out->minutes = cmos_read(REG_MINUTES);
    out->hours   = cmos_read(REG_HOURS);
    out->day     = cmos_read(REG_DAY);
    out->month   = cmos_read(REG_MONTH);
    out->year    = cmos_read(REG_YEAR);

    pt::uint8_t status_b = cmos_read(REG_STATUS_B);
    bool is_bcd = !(status_b & 0x04);

    if (is_bcd) {
        out->seconds = bcd_to_bin(out->seconds);
        out->minutes = bcd_to_bin(out->minutes);
        out->hours   = bcd_to_bin(out->hours);
        out->day     = bcd_to_bin(out->day);
        out->month   = bcd_to_bin(out->month);
        out->year    = bcd_to_bin(out->year);
    }

    // Expand 2-digit year: 00-99 → 2000-2099
    out->year += 2000;
}

