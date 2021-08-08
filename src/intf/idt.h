#pragma once
#include "defs.h"
#include "io.h"

struct IDT64
{
	uint16_t offset_low;
	uint16_t selector;
	uint8_t ist;
	uint8_t type_attr;
	uint16_t offset_mid;
	uint32_t offset_high;
	uint32_t zero;
};

class IDT
{
	public:
		void initialize();
};