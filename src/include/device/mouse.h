#pragma once
#include "defs.h"

constexpr pt::uint8_t CURSOR_WIDTH = 16;
constexpr pt::uint8_t CURSOR_HEIGHT = 16;
constexpr pt::uint8_t MIN_CURSOR_VISIBLE = 4;  // Minimum pixels that must stay on screen

struct mouse_state
{
    pt::int16_t pos_x;
    pt::int16_t pos_y;
    bool left_button_pressed;
    bool right_button_pressed;
};

// Raw mouse delta event queued on every PS/2 packet.
// dx/dy are the signed PS/2 deltas (dy positive = up).
struct MouseEvent {
    pt::int8_t dx;
    pt::int8_t dy;
    bool left_button;
    bool right_button;
};

void init_mouse(pt::int16_t max_x, pt::int16_t max_y);

void mouse_handler(pt::int8_t mouse_byte[]);

// Pop the next mouse event from the ring buffer.
// Returns false when the queue is empty.
bool get_mouse_event(MouseEvent* out);