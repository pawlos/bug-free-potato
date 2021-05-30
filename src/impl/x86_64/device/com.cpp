#include "com.h"
#include "io.h"
#include "print.h"

int is_transmit_empty() {
	return IO::inb(COM1 + 5) & 0x20;
}

void ComDevice::write_serial_ch(const char a) {
	while (is_transmit_empty() == 0);

	IO::outb(COM1, a);
}

void ComDevice::write_serial_str(const char *str, ...) {
	va_list ap;
	va_start(ap, str);
	for (size_t i=0; 1; i++) 
	{
		char character = (uint8_t)str[i];

		if (character == '\0') 
		{
			va_end(ap);
			return;
		}
		if (character == '%')
		{
			char next = (uint8_t)str[i+1];
			switch(next)
			{
				case '%':
				{
					write_serial_ch('%');
					i+=1;
					continue;
				}
				case 'x':
				{
					int a = va_arg(ap, int);
					write_serial_str("0x");
					write_serial_str(hexToString(a, false));
					i+=1;
					continue;
				}
				case 'X':
				{
					int a = va_arg(ap, int);
					write_serial_str("0x");
					write_serial_str(hexToString(a,true));
					i+=1;
					continue;
				}
				case 's':
				{
					char *a = va_arg(ap, char *);
					write_serial_str(a);
					i+=1;
					continue;
				}
			}
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
	
	write_serial_str("Serial Port initialized\n");
}



