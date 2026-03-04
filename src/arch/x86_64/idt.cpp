#include "idt.h"
#include "kernel.h"
#include "device/pic.h"

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
extern pt::uint64_t irq11;
extern pt::uint64_t irq12;
extern pt::uint64_t irq14;
extern pt::uint64_t irq15;
extern pt::uint64_t _syscall_stub;
extern pt::uint64_t _int_yield_stub;

ASMCALL void LoadIDT();

// type_attr = 0x8E: P=1, DPL=0, interrupt gate (ring-0 only)
// type_attr = 0xEE: P=1, DPL=3, interrupt gate (callable from ring-3)
void init_idt_entry(int irq_no, pt::uint64_t& irq, pt::uint8_t type_attr = 0x8e)
{
	_idt[irq_no].zero = 0;
	_idt[irq_no].offset_low  = (pt::uint16_t)((pt::uint64_t)&irq & 0x000000000000FFFF);
	_idt[irq_no].offset_mid  = (pt::uint16_t)(((pt::uint64_t)&irq & 0x00000000FFFF0000) >> 16);
	_idt[irq_no].offset_high = (pt::uint32_t)(((pt::uint64_t)&irq & 0xFFFFFFFF00000000) >> 32);
	_idt[irq_no].ist = 0;
	_idt[irq_no].selector = 0x08;
	_idt[irq_no].type_attr = type_attr;
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
	init_idt_entry(43, irq11);   // IRQ11 → vector 43 (32 + 11) — RTL8139 NIC
	init_idt_entry(44, irq12);
	init_idt_entry(46, irq14);
	init_idt_entry(47, irq15);

	init_idt_entry(0x80, _syscall_stub,   0xEE);  // DPL=3: ring-3 can call int 0x80
	init_idt_entry(0x81, _int_yield_stub, 0xEE);  // DPL=3: ring-3 can call int 0x81

	PIC::UnmaskAll();
	LoadIDT();
}
