#pragma once

#include "defs.h"

constexpr pt::uint8_t ModeCommandRegister = 0x43;
constexpr pt::uint8_t Channel0DataPort = 0x40;

struct Timer {
    pt::uint64_t id;
    pt::uint64_t deadline_ticks;
    pt::uint64_t interval;
    void (*callback)(void*);
    void* data;
    bool active;
    bool periodic;
    Timer* next;
};

void init_timer(pt::uint32_t freq);
pt::uint64_t get_ticks();
// Returns monotonic microseconds since boot, combining the tick counter with
// a live PIT channel-0 latch read for sub-tick (< 1 ms) resolution.
pt::uint64_t get_microseconds();
pt::uint64_t timer_create(pt::uint64_t delay_ticks, bool periodic, void (*callback)(void*), void* data);
void timer_cancel(pt::uint64_t timer_id);
void check_timers();
void timer_list_all();
// Called by irq0_schedule; increments ticks and fires callbacks (no scheduler call).
void timer_tick();
// Enable framebuffer flush in timer_tick (call after Framebuffer is fully initialized).
void enable_fb_flush();
