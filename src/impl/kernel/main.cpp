#include "print.h"
#include "com.h"
#include "boot.h"
#include "idt.h"
#include "kernel.h"
#include "framebuffer.h"
#include "virtual.h"

extern const char Logo[];
extern const unsigned char PotatoLogo[];
static BootInfo bi;
static IDT idt;

ASMCALL void kernel_main(boot_info* boot_info) {
	klog("Welcome to 64-bit potat OS\n");

	bi.parse(boot_info);

	idt.initialize();

	VMM vmm {bi.get_memory_maps()};

	Framebuffer fb {bi.get_framebuffer()};

	TerminalPrinter terminal;

	terminal.print_clear();	
	terminal.print_set_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK);
	terminal.print_str(Logo);

	fb.Draw(PotatoLogo, 0, 0, 197, 197);
	halt();
}