#pragma once
#include <stddef.h>
#include <stdint.h>

#define PIC1_COMMAND 0x20
#define PIC1_DATA	 0x21
#define PIC2_COMMAND 0xa0
#define PIC2_DATA    0xA1

#define ICW1_INIT	 0x10
#define ICW1_ICW4	 0x01
#define ICW4_8086	 0x01

class IO
{
public:
	static inline void outb(uint16_t port, uint8_t value) {
		asm volatile("out dx, al" :: "a"(value), "d"(port));
	}

	static inline uint8_t inb(uint16_t port) {
		uint8_t ret;
		asm volatile("in al, dx" : "=a"(ret) : "d"(port));
		return ret;
	}

	static void RemapPic()
	{
		uint8_t a1, a2;
		a1 = inb(PIC1_DATA);
		a2 = inb(PIC2_DATA);

		outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
		outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

		outb(PIC1_DATA, 0);
		outb(PIC2_DATA, 8);
		outb(PIC1_DATA, 4);
		outb(PIC2_DATA, 2);
		outb(PIC1_DATA, ICW4_8086);
		outb(PIC2_DATA, ICW4_8086);

		outb(PIC1_DATA, a1);
		outb(PIC2_DATA, a2);
	}
};