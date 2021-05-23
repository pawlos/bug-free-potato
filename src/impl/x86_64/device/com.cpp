#include "com.h"
#include "io.h"

int is_transmit_empty() {
	return IO::inb(COM1 + 5) & 0x20;
}

void ComDevice::write_serial_ch(const char a) {
	while (is_transmit_empty() == 0);

	IO::outb(COM1, a);
}

void ComDevice::write_serial_str(const char *str) {	
	for (size_t i=0; 1; i++) {
		char character = (uint8_t)str[i];

		if (character == '\0') {
			write_serial_ch('\n');
			return;
		}

		write_serial_ch(character);
	}
}

ComDevice::ComDevice() {
	IO::outb(COM1 + 1, 0x00);
	IO::outb(COM1 + 3, 0x80);
	IO::outb(COM1 + 0, 0x03);
	IO::outb(COM1 + 1, 0x00);
	IO::outb(COM1 + 3, 0x03);
	IO::outb(COM1 + 2, 0xC7);
	IO::outb(COM1 + 4, 0x0B);
	IO::outb(COM1 + 4, 0x1E);
	IO::outb(COM1 + 0, 0xAE);

	if (IO::inb(COM1 + 0) != 0xAE) {
		return;
	}

	IO::outb(COM1 + 4, 0x0F);
	
	write_serial_str("Serial Port initialized");
}



