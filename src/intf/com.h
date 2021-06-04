#pragma once
#include <stddef.h>
#include <stdint.h>

#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

class ComDevice 
{
	private:
		void print_ch(const char a);
	public:
		ComDevice();
		void print_str(const char *str, ...);
};