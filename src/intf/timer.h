#pragma once

#include "defs.h"

constexpr uint8_t ModeCommandRegister = 0x43;
constexpr uint8_t Channel0DataPort = 0x40;

void init_timer(uint32_t freq);