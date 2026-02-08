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

void init_mouse(pt::int16_t max_x, pt::int16_t max_y);

void mouse_handler(pt::int8_t mouse_byte[]);