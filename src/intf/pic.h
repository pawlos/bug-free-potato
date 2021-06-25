#pragma once
#include "io.h"

constexpr uint8_t Offset = 32;

class PIC
{
public:
	static void Remap();
	static void irq_ack(uint8_t irq);
};