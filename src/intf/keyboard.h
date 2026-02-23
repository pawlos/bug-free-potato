#pragma once
#include "defs.h"


constexpr pt::uint8_t L_SHIFT = 0x2A;
constexpr pt::uint8_t R_SHIFT = 0x36;
constexpr pt::uint8_t L_ALT = 0x38;
constexpr pt::uint8_t L_CTRL = 0x1D;
constexpr pt::uint8_t CAPSLOCK = 0x3A;

// Raw key event: scancode (PS/2 set-1, 0x01..0x7F) + pressed flag.
// Queued for both press and release so consumers (Doom) can react to both.
struct KeyEvent {
    pt::uint8_t scancode; // PS/2 set-1 make code (release bit stripped)
    bool        pressed;
};

void keyboard_routine(pt::uint8_t scancode);
char get_char();
// Returns false when the event queue is empty.
bool get_key_event(KeyEvent* out);