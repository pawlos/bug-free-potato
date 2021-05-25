#include "print.h"
#include "com.h"
#include "multiboot.h"
#include "idt.h"

extern const char Logo[];

void print_boot_info(TerminalPrinter terminal, multiboot_info* mbt)
{
	terminal.print_str("Boot info: \n");
	
	terminal.print_str("Flags: 0x");
	terminal.print_hex(mbt->flags);
	terminal.print_char('\n');
	
	terminal.print_str("Mem lower: 0x");
	terminal.print_hex(mbt->mem_lower);
	terminal.print_char('\n');

	terminal.print_str("Mem upper: 0x");
	terminal.print_hex(mbt->mem_upper);
	terminal.print_char('\n');

	terminal.print_str("Boot device: 0x");
	terminal.print_hex(mbt->boot_device);
	terminal.print_char('\n');

	terminal.print_str("Cmd line: ");
	terminal.print_str((const char *)(&mbt->cmdline));
	terminal.print_char('\n');

	terminal.print_str("Boot loader name: ");
	terminal.print_str((const char *)(&mbt->boot_loader_name));
	terminal.print_char('\n');
}

extern "C" void kernel_main(multiboot_info* mbt, unsigned int magic) {
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
	InitializeIDT();
	terminal.print_str("IDT inialiazed...\n");
	print_boot_info(terminal, mbt);
	for(;;) {
		asm("hlt");
	}
}