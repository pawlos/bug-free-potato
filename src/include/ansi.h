#pragma once
#include "defs.h"

constexpr pt::uint32_t ANSI_MAX_PARAMS = 8;

struct AnsiParser {
    enum State : pt::uint8_t { NORMAL, ESCAPE, CSI } state = NORMAL;
    pt::uint32_t params[ANSI_MAX_PARAMS] = {};
    pt::uint32_t n_params    = 0;
    bool         private_mode = false;

    void reset_csi() {
        for (pt::uint32_t i = 0; i < ANSI_MAX_PARAMS; i++) params[i] = 0;
        n_params      = 1;
        private_mode  = false;
    }

    // Returns true if caller should now dispatch (final CSI byte received).
    // Returns false while still accumulating.
    bool feed(char c, char& final_out) {
        switch (state) {
        case NORMAL:
            if (c == '\x1b') { state = ESCAPE; return false; }
            return false;

        case ESCAPE:
            if (c == '[') {
                state = CSI;
                reset_csi();
            } else {
                state = NORMAL;  // unknown; discard
            }
            return false;

        case CSI:
            if (c == '?') { private_mode = true; return false; }
            if (c >= '0' && c <= '9') {
                params[n_params - 1] = params[n_params - 1] * 10 + (pt::uint32_t)(c - '0');
                return false;
            }
            if (c == ';') {
                if (n_params < ANSI_MAX_PARAMS) n_params++;
                return false;
            }
            // Final byte
            state = NORMAL;
            final_out = c;
            return true;
        }
        return false;
    }
};

// Standard VGA/xterm 16-color palette
inline pt::uint32_t ansi_color(pt::uint32_t n) {
    constexpr pt::uint32_t pal[16] = {
        0x000000, 0xAA0000, 0x00AA00, 0xAA5500,
        0x0000AA, 0xAA00AA, 0x00AAAA, 0xAAAAAA,
        0x555555, 0xFF5555, 0x55FF55, 0xFFFF55,
        0x5555FF, 0xFF55FF, 0x55FFFF, 0xFFFFFF,
    };
    return (n < 16) ? pal[n] : 0xFFFFFF;
}
