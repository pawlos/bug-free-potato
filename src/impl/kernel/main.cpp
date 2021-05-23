#include "print.h"
#include "com.h"
#include "multiboot.h"
#include "idt.h"

extern const char Logo[];

extern "C" void kernel_main(struct multiboot_info* mbt, unsigned int magic) {
	TerminalPrinter terminal;
	terminal.print_clear();
	
	if (magic != 0x36d76289)
	{
	    terminal.print_str ("Invalid magic number.");
	    return;
	}
	terminal.print_set_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK);
	terminal.print_str(Logo);
	terminal.print_str("\n\n");
	terminal.print_str("Welcome to 64-bit potat OS\n");
	terminal.print_str("Boot info: \n");
	InitializeIDT();
	terminal.print_str("IDT inialiazed...\n");

	for(;;) {
		asm("hlt");
	}
}