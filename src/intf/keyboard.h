#pragma once
#include "defs.h"


constexpr uint8_t L_SHIFT = 0x2A;
constexpr uint8_t R_SHIFT = 0x3A;
constexpr uint8_t L_ALT = 0x38;
constexpr uint8_t L_CTRL = 0x1D;

void keyboard_routine(uint8_t scancode);