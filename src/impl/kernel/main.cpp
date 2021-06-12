#include "print.h"
#include "com.h"
#include "boot.h"
#include "idt.h"
#include "kernel.h"
#include "framebuffer.h"

extern const char Logo[];
extern const unsigned char PotatoLogo[];

ASMCALL void kernel_main(boot_info* boot_info) {
	ComDevice debug;
	debug.print_str("Welcome to 64-bit potat OS\n");
	BootInfo bi;
	bi.parse(boot_info);
	bi.print(&debug);

	IDT idt;
	idt.initialize();

	Framebuffer fb {bi.get_framebuffer()};

	TerminalPrinter terminal;

	terminal.print_clear();	
	terminal.print_set_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK);
	terminal.print_str(Logo);

	fb.Draw(PotatoLogo, 0, 0, 197, 197);
	halt();
}