#include "print.h"
#include "com.h"
#include "boot.h"
#include "idt.h"
#include "kernel.h"

extern const char Logo[];


extern "C" void kernel_main(boot_info* boot_info) {
	TerminalPrinter terminal;

	terminal.print_clear();	
	terminal.print_set_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK);
	terminal.print_str(Logo);
	terminal.print_str("\n\n");
	terminal.print_str("Welcome to 64-bit potat OS\n");

	IDT idt {&terminal};
	idt.initialize();

	BootInfo bi {&terminal};
	bi.parse(boot_info);

	halt();
}