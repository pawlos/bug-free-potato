#include "pic.h"


void PIC::Remap()
{
	uint8_t a1, a2;
	a1 = IO::inb(PIC1_DATA);
	a2 = IO::inb(PIC2_DATA);

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

	IO::outb(PIC1_DATA, a1);
	IO::outb(PIC2_DATA, a2);
}

void PIC::irq_ack(uint8_t irq)
{
	IO::outb(0x20, 0x20);
	if (irq > Offset)
		IO::outb(0x20, 0x20);
}