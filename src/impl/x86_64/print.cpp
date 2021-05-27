#include "print.h"

uint8_t color = PRINT_COLOR_WHITE | PRINT_COLOR_BLACK << 4;

void TerminalPrinter::clear_row(size_t row)
{
	struct Char empty = (struct Char)
	{
		character: ' ',
		color: color,
	};

	for (size_t col = 0; col < NUM_COLS; col++)
	{
		buffer[col + NUM_COLS * row] = empty;
	}
}


void TerminalPrinter::print_clear()
{
	for (size_t i = 0; i < NUM_ROWS; i++)
	{
		clear_row(i);
	}
}

void TerminalPrinter::set_cursor_position(uint8_t posx, uint8_t posy)
{
	this->col = posx;
	this->row = posy;
}

void TerminalPrinter::print_newline()
{
	col = 0;

	if (row < NUM_ROWS - 1)
	{
		row++;
		return;
	}

	for (size_t row = 1; row < NUM_ROWS; row++)
	{
		for (size_t col = 0; col < NUM_COLS; col ++)
		{
			struct Char character = buffer[col + NUM_COLS * row];
			buffer[col + NUM_COLS * (row - 1)] = character;
		}
	}
	clear_row(NUM_COLS - 1);
}

void TerminalPrinter::print_char(char character)
{
	if (character == '\n') {
		print_newline();
		return;
	}
	if (character == '\r') {
		col = 0;
		return;
	}

	if (col > NUM_COLS) {
		print_newline();
	}


	buffer[col + NUM_COLS * row] = (struct  Char)
	{
		character: (uint8_t)character,
		color: color,
	};
	col++;
}

const char* hexToString(uint64_t value);
void TerminalPrinter::print_str(const char *str, ...)
{
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
				case 'x':
				{
					int a = va_arg(ap, int);
					print_str("0x");
					print_str(hexToString(a));
					i+=1;
					continue;
				}
				case 's':
				{
					char *a = va_arg(ap, char *);
					print_str(a);
					i+=1;
					continue;
				}
			}
		}

		print_char(character);
	}
}

void TerminalPrinter::print_set_color(uint8_t foreground, uint8_t background)
{
	color = foreground + (background << 4);
}


char hexToStringOuput[128];
const char* hexToString(uint64_t value)
{
	uint8_t size = sizeof(value) * 2 - 1;
	uint64_t* valuePtr = &value;
	uint8_t* ptr;
	uint8_t temp;

	if (value != 0)
	{
		for (int i = 0; i < size; i++)
		{
			ptr = ((uint8_t*)valuePtr + i);

			temp = ((*ptr & 0xF0) >> 4);
			hexToStringOuput[size - ((i * 2 + 1))] = temp + (temp > 9 ? 55 : 48);

			temp = ((*ptr & 0x0F));
			hexToStringOuput[size - ((i * 2 + 0))] = temp + (temp > 9 ? 55 : 48);
		}
		hexToStringOuput[size + 1] = 0;
		int pos = 0;
		for (int i = 0; i < size * 2; i++, pos++)
		{
			if (hexToStringOuput[i] != '0') break;
		}
		return &hexToStringOuput[pos];
	}
	else
	{
		hexToStringOuput[0] = '0';
		hexToStringOuput[1] = '\0';
	}
	return &hexToStringOuput[0];
}

void TerminalPrinter::print_hex(uint64_t value)
{
	const char *str = hexToString(value);
	print_str(str);
}