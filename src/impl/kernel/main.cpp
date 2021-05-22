#include "print.h"
#include "com.h"
#include "multiboot.h"

extern "C" void kernel_main(struct multiboot_info* mbt, unsigned int magic) {
	ComDevice comDevice;
	TerminalPrinter terminal;
	terminal.print_clear();
	if (magic != 0x36d76289)
    {
      terminal.print_str ("Invalid magic number.");
      return;
    }
	terminal.print_set_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK);
	terminal.print_str("Welcome to 64-bit potato OS\n");
	terminal.print_str("Boot info: ");
}