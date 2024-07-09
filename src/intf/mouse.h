#pragma once
#include "defs.h"

struct mouse_state
{
	pt::uint16_t pos_x;
	pt::uint16_t pos_y;
	bool left_button_pressed;
	bool right_button_pressed;
};

void init_mouse(pt::uint16_t max_x, pt::uint16_t max_y);

void mouse_handler(pt::int8_t mouse_byte[]);