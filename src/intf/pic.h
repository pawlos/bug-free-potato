#pragma once
#include "io.h"

constexpr pt::uint8_t Offset = 32;
constexpr pt::uint8_t PIC1 = 0x20;
constexpr pt::uint8_t PIC2 = 0xA0;
constexpr pt::uint8_t PIC_EOI = 0x20;
constexpr pt::uint8_t PIC1_COMMAND = 0x20;
constexpr pt::uint8_t PIC1_DATA	= 0x21;
constexpr pt::uint8_t PIC2_COMMAND = 0xA0;
constexpr pt::uint8_t PIC2_DATA = 0xA1;

constexpr pt::uint8_t ICW1_INIT	= 0x10;
constexpr pt::uint8_t ICW1_ICW4	= 0x01;
constexpr pt::uint8_t ICW4_8086	= 0x01;

class PIC
{
public:
    static void Remap();
    static void UnmaskAll();
    static void irq_ack(pt::uint8_t irq);
};