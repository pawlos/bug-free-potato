#pragma once

#include "defs.h"

constexpr pt::uint8_t ModeCommandRegister = 0x43;
constexpr pt::uint8_t Channel0DataPort = 0x40;

void init_timer(pt::uint32_t freq);

pt::uint64_t get_ticks();