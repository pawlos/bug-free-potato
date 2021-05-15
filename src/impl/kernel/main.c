#include "print.h"
#include "com.h"

void kernel_main() {
	initCom();
	print_clear();
	print_set_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK);
	print_str("Welcome to 64-bit potato OS");

}