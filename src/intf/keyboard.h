#pragma once
#include "defs.h"


constexpr pt::uint8_t L_SHIFT = 0x2A;
constexpr pt::uint8_t R_SHIFT = 0x36;
constexpr pt::uint8_t L_ALT = 0x38;
constexpr pt::uint8_t L_CTRL = 0x1D;
constexpr pt::uint8_t CAPSLOCK = 0x3A;

void keyboard_routine(pt::uint8_t scancode);