#pragma once
#include "defs.h"

struct RTCTime {
    pt::uint8_t  seconds;
    pt::uint8_t  minutes;
    pt::uint8_t  hours;
    pt::uint8_t  day;
    pt::uint8_t  month;
    pt::uint16_t year;
};

// Read the current time from the CMOS RTC.
// Waits for any in-progress update to finish before reading.
void rtc_read(RTCTime* out);

