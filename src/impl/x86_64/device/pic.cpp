#include "pic.h"


void PIC::Remap()
{
	IO::outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
	IO::io_wait();
	IO::outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
	IO::io_wait();

	IO::outb(PIC1_DATA, Offset);
	IO::io_wait();
	IO::outb(PIC2_DATA, Offset+8);
	IO::io_wait();
	IO::outb(PIC1_DATA, 4);
	IO::io_wait();
	IO::outb(PIC2_DATA, 2);
	IO::io_wait();
	IO::outb(PIC1_DATA, ICW4_8086);
	IO::io_wait();
	IO::outb(PIC2_DATA, ICW4_8086);
	IO::io_wait();
}

void PIC::UnmaskAll()
{
	IO::outb(0x21, 0);
	IO::outb(0xa1, 0);
}

void PIC::irq_ack(pt::uint8_t irq)
{
	if (irq >= 8)
		IO::outb(PIC2, PIC_EOI);
	IO::outb(PIC1, PIC_EOI);
}