#pragma once
#include "defs.h"
#include <cstdarg>

constexpr pt::uint32_t COM1 = 0x3F8;
constexpr pt::uint32_t COM2 = 0x2F8;
constexpr pt::uint32_t COM3 = 0x3E8;
constexpr pt::uint32_t COM4 = 0x2E8;

class ComDevice 
{
	private:
		void print_ch(const char a);
	public:
		ComDevice();
		void print_str(const char *str, va_list args);
		void print_str(const char *str, ...);
};