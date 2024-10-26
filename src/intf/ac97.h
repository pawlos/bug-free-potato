#pragma once
#include <cstdint>


class ac97 {
public:
    ac97() = default;
    void reset();
    void set_volume(uint8_t vol);
};
