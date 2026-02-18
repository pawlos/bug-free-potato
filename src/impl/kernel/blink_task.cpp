#include "blink_task.h"
#include "defs.h"
#include "framebuffer.h"
#include "fbterm.h"
#include "rtc.h"
#include "timer.h"
#include "task.h"

void blink_task_fn() {
    Framebuffer* fb = Framebuffer::get_instance();

    constexpr pt::uint32_t DOT_W   = 12;
    constexpr pt::uint32_t DOT_H   = 12;
    // "HH:MM" = 5 chars * 8px wide = 40px, plus 4px gap before dot
    constexpr pt::uint32_t CLOCK_W = 5 * PSF1_GLYPH_WIDTH;
    constexpr pt::uint32_t GAP     = 4;

    const pt::uint32_t DOT_X   = fb->get_width() - DOT_W;
    const pt::uint32_t DOT_Y   = 0;
    const pt::uint32_t CLOCK_X = DOT_X - GAP - CLOCK_W;
    const pt::uint32_t CLOCK_Y = 0;

    bool dot_on = false;
    pt::uint8_t last_minute = 0xFF;  // force draw on first iteration
    char time_buf[6];                // "HH:MM\0"

    while (true) {
        // Blink dot every ~0.5s
        bool should_be_on = (get_ticks() / 25) & 1;
        if (should_be_on != dot_on) {
            dot_on = should_be_on;
            if (dot_on)
                fb->FillRect(DOT_X, DOT_Y, DOT_W, DOT_H, 0, 255, 0);
            else
                fb->FillRect(DOT_X, DOT_Y, DOT_W, DOT_H, 0, 0, 0);
        }

        // Update clock display once per minute
        RTCTime t;
        rtc_read(&t);
        if (t.minutes != last_minute) {
            last_minute = t.minutes;
            time_buf[0] = '0' + t.hours / 10;
            time_buf[1] = '0' + t.hours % 10;
            time_buf[2] = ':';
            time_buf[3] = '0' + t.minutes / 10;
            time_buf[4] = '0' + t.minutes % 10;
            time_buf[5] = '\0';
            fbterm.draw_at(CLOCK_X, CLOCK_Y, time_buf, 0x00FF00, 0x000000);
        }

        TaskScheduler::task_yield();
    }
}
