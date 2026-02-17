#include "idt.h"
#include "com.h"
#include "kernel.h"
#include "pic.h"

extern IDT64 _idt[256];
extern pt::uint64_t isr0;
extern pt::uint64_t isr1;
extern pt::uint64_t isr2;
extern pt::uint64_t isr3;
extern pt::uint64_t isr4;
extern pt::uint64_t isr5;
extern pt::uint64_t isr6;
extern pt::uint64_t isr7;
extern pt::uint64_t isr8;
extern pt::uint64_t isr9;
extern pt::uint64_t isr10;
extern pt::uint64_t isr11;
extern pt::uint64_t isr12;
extern pt::uint64_t isr13;
extern pt::uint64_t isr14;
extern pt::uint64_t isr15;
extern pt::uint64_t isr16;
extern pt::uint64_t isr17;
extern pt::uint64_t isr18;
extern pt::uint64_t isr19;
extern pt::uint64_t isr20;
extern pt::uint64_t isr21;
extern pt::uint64_t isr22;
extern pt::uint64_t isr23;
extern pt::uint64_t isr24;
extern pt::uint64_t isr25;
extern pt::uint64_t isr26;
extern pt::uint64_t isr27;
extern pt::uint64_t isr28;
extern pt::uint64_t isr29;
extern pt::uint64_t isr30;
extern pt::uint64_t isr31;
extern pt::uint64_t irq0;
extern pt::uint64_t irq1;
extern pt::uint64_t irq12;
extern pt::uint64_t irq14;
extern pt::uint64_t irq15;

extern void keyboard_routine(pt::uint8_t scancode);
extern void mouse_routine(const pt::int8_t mouse[]);
extern void timer_routine();
ASMCALL void LoadIDT();

void init_idt_entry(int irq_no, pt::uint64_t& irq)
{
	_idt[irq_no].zero = 0;
	_idt[irq_no].offset_low  = (pt::uint16_t)((pt::uint64_t)&irq & 0x000000000000FFFF);
	_idt[irq_no].offset_mid  = (pt::uint16_t)(((pt::uint64_t)&irq & 0x00000000FFFF0000) >> 16);
	_idt[irq_no].offset_high = (pt::uint32_t)(((pt::uint64_t)&irq & 0xFFFFFFFF00000000) >> 32);
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
	init_idt_entry(7, isr7);
	init_idt_entry(8, isr8);
	init_idt_entry(9, isr9);
	init_idt_entry(10, isr10);
	init_idt_entry(11, isr11);
	init_idt_entry(12, isr12);
	init_idt_entry(13, isr13);
	init_idt_entry(14, isr14);
	init_idt_entry(15, isr15);
	init_idt_entry(16, isr16);
	init_idt_entry(17, isr17);
	init_idt_entry(18, isr18);
	init_idt_entry(19, isr19);
	init_idt_entry(20, isr20);
	init_idt_entry(21, isr21);
	init_idt_entry(22, isr22);
	init_idt_entry(23, isr23);
	init_idt_entry(24, isr24);
	init_idt_entry(25, isr25);
	init_idt_entry(26, isr26);
	init_idt_entry(27, isr27);
	init_idt_entry(28, isr28);
	init_idt_entry(29, isr29);
	init_idt_entry(30, isr30);
	init_idt_entry(31, isr31);

	init_idt_entry(32, irq0);
	init_idt_entry(33, irq1);
	init_idt_entry(44, irq12);
	init_idt_entry(46, irq14);
	init_idt_entry(47, irq15);

	PIC::UnmaskAll();
	LoadIDT();
}

ASMCALL void irq0_handler()
{
	timer_routine();
	PIC::irq_ack(0);
}

ASMCALL void irq1_handler()
{
	const pt::uint8_t c = IO::inb(0x60);
	keyboard_routine(c);
	PIC::irq_ack(1);
}

pt::uint8_t mouse_cycle=0;
pt::int8_t  mouse_byte[3];

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

ASMCALL void irq14_handler()
{
	// IDE primary channel interrupt - just acknowledge it
	// We're using polling, so we don't need to do anything here
	PIC::irq_ack(14);
}

ASMCALL void irq15_handler()
{
	// IDE secondary channel interrupt - just acknowledge it
	// We're using polling, so we don't need to do anything here
	PIC::irq_ack(15);
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

ASMCALL void isr7_handler()
{
	kernel_panic("Device not available", 7);
}

ASMCALL void isr9_handler()
{
	kernel_panic("Coprocessor segment overrun", 9);
}

ASMCALL void isr10_handler()
{
	kernel_panic("Invalid TSS", 10);
}

ASMCALL void isr11_handler()
{
	kernel_panic("Segment not present", 11);
}

ASMCALL void isr12_handler()
{
	kernel_panic("Stack segment fault", 12);
}

ASMCALL void isr8_handler()
{
	kernel_panic("Double fault", 8);
}

ASMCALL void isr13_handler()
{
	kernel_panic("General protection fault", 13);
}

ASMCALL void isr14_handler()
{
	kernel_panic("Page fault", 14);
}

ASMCALL void isr15_handler()
{
	kernel_panic("Reserved exception 15", 15);
}

ASMCALL void isr16_handler()
{
	kernel_panic("Floating point exception", 16);
}

ASMCALL void isr17_handler()
{
	kernel_panic("Alignment check", 17);
}

ASMCALL void isr18_handler()
{
	kernel_panic("Machine check", 18);
}

ASMCALL void isr19_handler()
{
	kernel_panic("SIMD floating point exception", 19);
}

ASMCALL void isr20_handler()
{
	kernel_panic("Reserved exception 20", 20);
}

ASMCALL void isr21_handler()
{
	kernel_panic("Reserved exception 21", 21);
}

ASMCALL void isr22_handler()
{
	kernel_panic("Reserved exception 22", 22);
}

ASMCALL void isr23_handler()
{
	kernel_panic("Reserved exception 23", 23);
}

ASMCALL void isr24_handler()
{
	kernel_panic("Reserved exception 24", 24);
}

ASMCALL void isr25_handler()
{
	kernel_panic("Reserved exception 25", 25);
}

ASMCALL void isr26_handler()
{
	kernel_panic("Reserved exception 26", 26);
}

ASMCALL void isr27_handler()
{
	kernel_panic("Reserved exception 27", 27);
}

ASMCALL void isr28_handler()
{
	kernel_panic("Reserved exception 28", 28);
}

ASMCALL void isr29_handler()
{
	kernel_panic("Reserved exception 29", 29);
}

ASMCALL void isr30_handler()
{
	kernel_panic("Reserved exception 30", 30);
}

ASMCALL void isr31_handler()
{
	kernel_panic("Reserved exception 31", 31);
}

ASMCALL void syscall_handler()
{
	klog("Syscall executed");
}