#include "com.h"

void outb(uint16_t port, uint8_t value) {
	asm volatile("out dx, al" :: "a"(value), "d"(port));
}

uint8_t inb(uint16_t port) {
	uint8_t ret;
	asm volatile("in al, dx" : "=a"(ret) : "d"(port));
	return ret;
}

int is_transmit_empty() {
	return inb(COM1 + 5) & 0x20;
}

void write_serial(char a) {
	while (is_transmit_empty() == 0);

	outb(COM1, a);
}

void initCom() {
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x80);
	outb(COM1 + 0, 0x03);
	outb(COM1 + 1, 0x00);
	outb(COM1 + 3, 0x03);
	outb(COM1 + 2, 0xC7);
	outb(COM1 + 4, 0x0B);
	outb(COM1 + 4, 0x1E);
	outb(COM1 + 0, 0xAE);

	if (inb(COM1 + 0) != 0xAE) {
		return;
	}

	outb(COM1 + 4, 0x0F);
	
	write_serial('o');
	write_serial('k');
}



