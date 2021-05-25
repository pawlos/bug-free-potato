#pragma once

#include <stdint.h>
#include <stddef.h>

enum {
	PRINT_COLOR_BLACK = 0,
	PRINT_COLOR_BLUE = 1,
	PRINT_COLOR_GREEN = 2,
	PRINT_COLOR_CYAN = 3,
	PRINT_COLOR_RED = 4,
	PRINT_COLOR_MAGENTA = 5,
	PRINT_COLOR_BROWN = 6,
	PRINT_COLOR_LIGHT_GRAY = 7,
	PRINT_COLOR_DARK_GRAY = 8,
	PRINT_COLOR_LIGHT_BLUE = 9,
	PRINT_COLOR_LIGHT_GREEN = 10,
	PRINT_COLOR_LIGHT_CYAN = 11,
	PRINT_COLOR_LIGHT_RED = 12,
	PRINT_COLOR_PINK = 13,
	PRINT_COLOR_YELLOW = 14,
	PRINT_COLOR_WHITE = 15,
};

const static size_t NUM_COLS = 80;
const static size_t NUM_ROWS = 25;

class TerminalPrinter
{
	struct Char {
		uint8_t character;
		uint8_t color;
	};

	void clear_row(size_t row);
	void print_newline();
	struct Char* buffer = (struct Char*)0xb8000;
	size_t col = 0;
	size_t row = 0;
	
	public:
		void print_clear();
		void print_char(const char character);
		void print_str(const char *str);
		void print_set_color(uint8_t foreground, uint8_t background);
		void set_cursor_position(uint8_t posx, uint8_t posy);
		void print_hex(const uint64_t value);
};