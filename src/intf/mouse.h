#pragma once
#include "defs.h"
#include "kernel.h"

struct mouse_state
{
	uint16_t pos_x;
	uint16_t pos_y;
	bool left_button_pressed;
	bool right_button_pressed;
};

void init_mouse();

void mouse_handler(mouse_state mouse);