#include "idt.h"
#include "com.h"
#include "kernel.h"

extern IDT64 _idt[256];
extern uint64_t isr1;
extern uint64_t isr0;
extern "C" void LoadIDT();

void init_idt_entry(int irq_no, uint64_t& irq)
{
	_idt[irq_no].zero = 0;
	_idt[irq_no].offset_low  = (uint16_t)(((uint64_t)&irq & 0x000000000000FFFF));
	_idt[irq_no].offset_mid  = (uint16_t)(((uint64_t)&irq & 0x00000000FFFF0000) >> 16);
	_idt[irq_no].offset_high = (uint32_t)(((uint64_t)&irq & 0xFFFFFFFF00000000) >> 32);
	_idt[irq_no].ist = 0;
	_idt[irq_no].selector = 0x08;
	_idt[irq_no].types_addr = 0x8e;
}

void IDT::initialize()
{		
	init_idt_entry(0, isr0);
	init_idt_entry(1, isr1);
	
	IO::RemapPic();

	IO::outb(0x21, 0xfd);
	IO::outb(0xa1, 0xff);
	LoadIDT();
	m_terminal->print_str("IDT initialized...\n");
}

extern "C" void isr1_handler()
{
	uint8_t c = IO::inb(0x60);

	IO::outb(0x20, 0x20);
}

extern "C" void isr0_handler()
{	
	IO::outb(0x20, 0x20);
	kernel_panic(0);
}