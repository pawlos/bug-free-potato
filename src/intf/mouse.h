#pragma once
#include "defs.h"

struct mouse_state
{
	uint16_t pos_x;
	uint16_t pos_y;
	bool left_button_pressed;
	bool right_button_pressed;
};

void init_mouse(uint16_t max_x, uint16_t max_y);

void mouse_handler(int8_t mouse_byte[]);