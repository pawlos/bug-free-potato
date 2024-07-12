#include "print.h"
#include <cstdarg>

pt::uint8_t color = PRINT_COLOR_WHITE | PRINT_COLOR_BLACK << 4;

void TerminalPrinter::clear_row(pt::size_t row)
{
	auto empty = (struct Char)
	{
		.character = ' ',
		.color = color,
	};

	for (pt::size_t col = 0; col < NUM_COLS; col++)
	{
		buffer[col + NUM_COLS * row] = empty;
	}
}


void TerminalPrinter::print_clear()
{
	for (pt::size_t i = 0; i < NUM_ROWS; i++)
	{
		clear_row(i);
	}
}

void TerminalPrinter::set_cursor_position(pt::uint8_t posx, pt::uint8_t posy)
{
	this->col = posx;
	this->row = posy;
}

void TerminalPrinter::freeze_rows(pt::size_t num)
{
	this->frozen_rows = num;
}

void TerminalPrinter::print_newline()
{
	col = 0;

	if (row < NUM_ROWS - 1)
	{
		row++;
		return;
	}

	for (pt::size_t row = 1 + this->frozen_rows; row < NUM_ROWS; row++)
	{
		for (pt::size_t col = 0; col < NUM_COLS; col ++)
		{
			const Char character = buffer[col + NUM_COLS * row];
			buffer[col + NUM_COLS * (row - 1)] = character;
		}
	}
	clear_row(NUM_ROWS - 1);
}

void TerminalPrinter::print_char(const char character)
{
	if (character == '\n')
	{
		print_newline();
		return;
	}
	if (character == '\r')
	{
		col = 0;
		return;
	}
	if (character == '\t')
	{
		const pt::size_t backup = col;
		col += 4 - col % 4;
		for (pt::size_t i = backup; i < col; i++)
		{
			buffer[i + NUM_COLS * row] = (Char)
			{
				.character=(pt::uint8_t)' ',
				.color=color,
			};
		}
		col = col % NUM_COLS;
		return;
	}

	if (col > NUM_COLS) {
		print_newline();
	}


	buffer[col + NUM_COLS * row] = (Char)
	{
		.character=(pt::uint8_t)character,
		.color=color,
	};
	col++;
}

template<typename T> const char* hexToString(T value, bool upper);
void TerminalPrinter::print_str(const char *str, ...)
{
	va_list ap;
	va_start(ap, str);
	for (pt::size_t i=0; true; i++)
	{
		const char character = (pt::uint8_t)str[i];

		if (character == '\0') 
		{
			va_end(ap);
			return;
		}

		if (character == '%')
		{
			char next = (pt::uint8_t)str[i+1];
			switch(next)
			{
				case '%':
				{
					print_char('%');
					i+=1;
					continue;
				}
				case 'd':
				{
					int a = va_arg(ap, int);
					print_str(decToString(a));
					i += 1;
					continue;
				}
				case 's':
				{
					const char *a = va_arg(ap, const char *);
					print_str(a);
					i+=1;
					continue;
				}
				case 'p':
				{
					pt::size_t ptr = va_arg(ap, pt::size_t);
					print_str("0x%s", hexToString(ptr, false));
					i+=1;
					continue;
				}
				case 'x':
				{
					pt::uint64_t a = va_arg(ap, pt::uint64_t);
					print_str("0x%s", hexToString(a, false));
					i+=1;
					continue;
				}
				case 'X':
				{
					pt::uint64_t a = va_arg(ap, pt::uint64_t);
					print_str("0x%s", hexToString(a,true));
					i+=1;
					continue;
				}
			}
		}

		print_char(character);
	}
}

void TerminalPrinter::print_set_color(const pt::uint8_t foreground, const pt::uint8_t background)
{
	color = foreground + (background << 4);
}

char decToStringOutput[128];
template<typename T> const char* decToString(T value)
{
	T copy = value;
	bool isNegative = false;
	if (copy < 0)
	{
		isNegative = true;
		copy = -copy;
	}
	int i = 0;
	while (copy/10 > 0)
	{
		decToStringOutput[i] = copy % 10 + 48;
		copy /= 10;
		i++;
	}
	decToStringOutput[i] = copy%10 + 48;
	i++;
	if (isNegative)
	{
		decToStringOutput[i] = '-';
		i++;
	}
	decToStringOutput[i] = 0;
	for (int j = 0; j < i/2; j++)
	{
		char tmp = decToStringOutput[j];
		decToStringOutput[j] = decToStringOutput[i - j - 1];
		decToStringOutput[i - j - 1] = tmp;
	}

	return &decToStringOutput[0];
}

char hexToStringOuput[128];
template<typename T> const char* hexToString(T value, bool upper)
{
	const pt::uint8_t size = sizeof(value) * 2 - 1;
	T* valuePtr = &value;
	const int offset = upper ? 55 : 87;
	if (value != 0)
	{
		for (int i = 0; i < size; i++)
		{
			const pt::uint8_t *ptr = (pt::uint8_t *) valuePtr + i;

			pt::uint8_t temp = (*ptr & 0xF0) >> 4;
			hexToStringOuput[size - (i * 2 + 1)] = temp + (temp > 9 ? offset : 48);

			temp = *ptr & 0x0F;
			hexToStringOuput[size - (i * 2 + 0)] = temp + (temp > 9 ? offset : 48);
		}
		hexToStringOuput[size + 1] = 0;
		int pos = 0;
		for (int i = 0; i < size * 2; i++, pos++)
		{
			if (hexToStringOuput[i] != '0') break;
		}
		return &hexToStringOuput[pos];
	}
	hexToStringOuput[0] = '0';
	hexToStringOuput[1] = '\0';
	return &hexToStringOuput[0];
}