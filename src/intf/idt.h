#pragma once
#include "defs.h"

struct IDT64
{
    pt::uint16_t offset_low;
    pt::uint16_t selector;
    pt::uint8_t ist;
    pt::uint8_t type_attr;
    pt::uint16_t offset_mid;
    pt::uint32_t offset_high;
    pt::uint32_t zero;
};

class IDT
{
    public:
        void initialize();
};