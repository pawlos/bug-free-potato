#include "idt.h"
#include "com.h"

extern IDT64 _idt[256];
extern uint64_t isr1;
extern "C" void LoadIDT();

void InitializeIDT()
{		
	_idt[1].zero = 0;
	_idt[1].offset_low  = (uint16_t)(((uint64_t)&isr1 & 0x000000000000FFFF));
	_idt[1].offset_mid  = (uint16_t)(((uint64_t)&isr1 & 0x00000000FFFF0000) >> 16);
	_idt[1].offset_high = (uint32_t)(((uint64_t)&isr1 & 0xFFFFFFFF00000000) >> 32);
	_idt[1].ist = 0;
	_idt[1].selector = 0x08;
	_idt[1].types_addr = 0x8e;
	
	IO::RemapPic();

	IO::outb(0x21, 0xfd);
	IO::outb(0xa1, 0xff);
	LoadIDT();
}

extern "C" void isr1_handler()
{
	uint8_t c = IO::inb(0x60);

	IO::outb(0x20, 0x20);
}