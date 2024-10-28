#pragma once
#include "io.h"


class ac97 {
    pt::uintptr_t base_address_0;
    pt::uintptr_t base_address_1;
    pt::uintptr_t command_reg;
public:
    ac97(const pt::uintptr_t bar0, const pt::uintptr_t bar1, const pt::uintptr_t command_reg) : base_address_0(bar0), base_address_1(bar1), command_reg(command_reg) {
        IO::outd(command_reg, IO::ind(command_reg) | 0b101);
    }
    void reset();
    void set_volume(pt::uint8_t vol);
};
