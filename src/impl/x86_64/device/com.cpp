#include "com.h"
#include "io.h"
#include "print.h"
#include <cstdarg>

int is_transmit_empty()
{
	return IO::inb(COM1 + 5) & 0x20;
}

void ComDevice::print_ch(const char a)
{
	while (is_transmit_empty() == 0) {}

	IO::outb(COM1, a);
}

void ComDevice::print_str(const char *str, ...)
{
	va_list ap;
	va_start(ap, str);
	print_str(str, ap);
	va_end(ap);
}

void ComDevice::print_str(const char *str, va_list args)
{
	for (pt::size_t i=0; true; i++)
	{
		const char character = (pt::uint8_t)str[i];

		if (character == '\0') 
		{
			va_end(args);
			return;
		}
		if (character == '%')
		{
			char next = (pt::uint8_t)str[i+1];
			switch(next)
			{
				case '%':
				{
					print_ch('%');
					i+=1;
					continue;
				}
				case 'i':
				case 'd':
				{
					int a = va_arg(args, int);
					print_str(decToString(a));
					i += 1;
					continue;
				}
				case 'l':
				{
					const pt::uint64_t a = va_arg(args, pt::uint64_t);
					print_str(decToString(a));
					i += 1;
					continue;
				}
				case 'p':
				{
					pt::size_t ptr = va_arg(args, pt::size_t);
					print_str("0x%s", hexToString(ptr, false));
					i+=1;
					continue;
				}
				case 's':
				{
					const char *a = va_arg(args, const char *);
					print_str(a);
					i+=1;
					continue;
				}
				case 'c':
				{
					const char a = va_arg(args, int);
					print_ch(a);
					i+=1;
					continue;
				}
				case 'x':
				{
					pt::uint64_t a = va_arg(args, pt::uint64_t);
					print_str("0x%s", hexToString(a, false));
					i+=1;
					continue;
				}
				case 'b':
				{
					pt::uint64_t a = va_arg(args, pt::uint64_t);
					print_str("0b%s", binToString(a));
					i+=1;
					continue;
				}
				case 'X':
				{
					pt::uint64_t a = va_arg(args, pt::uint64_t);
					print_str("0x%s", hexToString(a,true));
					i+=1;
					continue;
				}
			}
		}

		print_ch(character);
	}
}

ComDevice::ComDevice()
{
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
	
	print_str("Serial Port initialized\n");
}
