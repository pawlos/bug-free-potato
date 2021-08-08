#include "idt.h"
#include "com.h"
#include "kernel.h"
#include "pic.h"

extern IDT64 _idt[256];
extern uint64_t isr0;
extern uint64_t isr1;
extern uint64_t isr2;
extern uint64_t isr3;
extern uint64_t isr4;
extern uint64_t isr5;
extern uint64_t isr6;
extern uint64_t isr8;
extern uint64_t isr14;
extern uint64_t irq0;
extern uint64_t irq1;
extern uint64_t irq12;

extern void keyboard_routine(uint8_t scancode);
extern void mouse_routine(int8_t mouse[]);
extern void timer_routine();
ASMCALL void LoadIDT();

void init_idt_entry(int irq_no, uint64_t& irq)
{
	_idt[irq_no].zero = 0;
	_idt[irq_no].offset_low  = (uint16_t)(((uint64_t)&irq & 0x000000000000FFFF));
	_idt[irq_no].offset_mid  = (uint16_t)(((uint64_t)&irq & 0x00000000FFFF0000) >> 16);
	_idt[irq_no].offset_high = (uint32_t)(((uint64_t)&irq & 0xFFFFFFFF00000000) >> 32);
	_idt[irq_no].ist = 0;
	_idt[irq_no].selector = 0x08;
	_idt[irq_no].type_attr = 0x8e;
}

void IDT::initialize()
{
	PIC::Remap();

	init_idt_entry(0, isr0);
	init_idt_entry(1, isr1);
	init_idt_entry(2, isr2);
	init_idt_entry(3, isr3);
	init_idt_entry(4, isr4);
	init_idt_entry(5, isr5);
	init_idt_entry(6, isr6);
	init_idt_entry(8, isr8);
	init_idt_entry(14, isr14);

	init_idt_entry(32, irq0);
	init_idt_entry(33, irq1);
	init_idt_entry(44, irq12);

	PIC::Mask();
	LoadIDT();
}

ASMCALL void irq0_handler()
{
	timer_routine();
	PIC::irq_ack(0);
}

ASMCALL void irq1_handler()
{
	uint8_t c = IO::inb(0x60);
	keyboard_routine(c);
	PIC::irq_ack(1);
}

uint8_t mouse_cycle=0;
int8_t  mouse_byte[3];

ASMCALL void irq12_handler()
{
	switch(mouse_cycle)
	{
		case 0:
			mouse_byte[0] = IO::inb(0x60);
			mouse_cycle++;
			break;
		case 1:
			mouse_byte[1] = IO::inb(0x60);
			mouse_cycle++;
			break;
		case 2:
			mouse_byte[2] = IO::inb(0x60);
			mouse_cycle=0;
			mouse_routine(mouse_byte);
			break;
	}
	PIC::irq_ack(12);
}

ASMCALL void isr0_handler()
{
	kernel_panic("Divide by zero", 0);
}

ASMCALL void isr1_handler()
{
	kernel_panic("Debug", 0);
}

ASMCALL void isr2_handler()
{
	kernel_panic("NMI", 2);
}

ASMCALL void isr3_handler()
{
	kernel_panic("Debug", 3);
}

ASMCALL void isr4_handler()
{
	kernel_panic("Overflow", 4);
}

ASMCALL void isr5_handler()
{
	kernel_panic("Bound range exceeded", 5);
}

ASMCALL void isr6_handler()
{
	kernel_panic("Invalid opcode", 6);
}

ASMCALL void isr8_handler()
{
	kernel_panic("Double fault", 8);
}

ASMCALL void isr14_handler()
{
	kernel_panic("Page fault", 14);
}