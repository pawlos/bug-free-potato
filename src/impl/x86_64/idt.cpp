#include "idt.h"
#include "com.h"

extern IDT64 _idt[256];
extern uint64_t isr1;
extern "C" void LoadIDT();

void InitializeIDT()
{	
	for (uint64_t i=0; i<256; i++)
	{
		_idt[i].zero = 0;
		_idt[i].offset_low  = (uint16_t)(((uint64_t)&isr1 & 0x000000000000FFFF));
		_idt[i].offset_mid  = (uint16_t)(((uint64_t)&isr1 & 0x00000000FFFF0000) >> 16);
		_idt[i].offset_high = (uint32_t)(((uint64_t)&isr1 & 0xFFFFFFFF00000000) >> 32);
		_idt[i].ist = 0;
		_idt[i].selector = 0x08;
		_idt[i].types_addr = 0x8e;
	}

	IO::outb(0x21, 0xfd);
	IO::outb(0xa1, 0xff);
	LoadIDT();
}

extern "C" void isr1_handler()
{
}