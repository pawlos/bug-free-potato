#include "device/pic.h"


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
	// Only unmask IRQs that have registered handlers:
	//   Master: 0 (timer), 1 (keyboard), 2 (slave cascade)
	//   Slave:  3=RTL8139/IRQ11, 4=mouse/IRQ12, 6=IDE1/IRQ14, 7=IDE2/IRQ15
	// All other IRQs stay masked until a driver sets them up.
	IO::outb(PIC1_DATA, 0xF8);   // mask IRQs 3-7
	IO::outb(PIC2_DATA, 0x27);   // unmask IRQ11,12,14,15 only
}

void PIC::irq_ack(pt::uint8_t irq)
{
	if (irq >= 8)
		IO::outb(PIC2, PIC_EOI);
	IO::outb(PIC1, PIC_EOI);
}

void PIC::mask_irq(pt::uint8_t irq)
{
	pt::uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
	pt::uint8_t bit = irq < 8 ? irq : irq - 8;
	pt::uint8_t mask = IO::inb(port);
	mask |= (1u << bit);
	IO::outb(port, mask);
}

void PIC::unmask_irq(pt::uint8_t irq)
{
	pt::uint16_t port = irq < 8 ? PIC1_DATA : PIC2_DATA;
	pt::uint8_t bit = irq < 8 ? irq : irq - 8;
	pt::uint8_t mask = IO::inb(port);
	mask &= ~(1u << bit);
	IO::outb(port, mask);
}