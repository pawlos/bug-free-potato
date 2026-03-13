#pragma once
#include "defs.h"


constexpr pt::uint8_t L_SHIFT = 0x2A;
constexpr pt::uint8_t R_SHIFT = 0x36;
constexpr pt::uint8_t L_ALT = 0x38;
constexpr pt::uint8_t L_CTRL = 0x1D;
constexpr pt::uint8_t CAPSLOCK = 0x3A;
constexpr pt::uint8_t L_WIN = 0x5B;  // E0-prefixed left Windows/Super key

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
// Discard all pending key events.
void flush_key_events();
// Translate a PS/2 set-1 scancode to its ASCII character using the current
// shift/caps state.  Returns 0 for non-printable / modifier keys.
char keyboard_scancode_to_char(pt::uint8_t scancode);
// Returns true (and clears) if the Windows/Super key was pressed since last check.
bool consume_start_key();