#include "print.h"
#include "com.h"
#include "boot.h"
#include "idt.h"
#include "kernel.h"
#include "framebuffer.h"
#include "virtual.h"
#include "timer.h"
#include "mouse.h"

extern const char Logo[];
extern const unsigned char PotatoLogo[];
static BootInfo bi;
static IDT idt;
VMM vmm = nullptr;


ASMCALL void kernel_main(boot_info* boot_info, void* l4_page_table) {
	klog("[MAIN] Welcome to 64-bit potat OS\n");

	bi.parse(boot_info);

	const auto boot_fb = bi.get_framebuffer();

	init_mouse(boot_fb->framebuffer_width, boot_fb->framebuffer_height);
	init_timer(50);

	idt.initialize();

	vmm = VMM(bi.get_memory_maps());

	Framebuffer::Init(boot_fb);

	TerminalPrinter terminal;

	terminal.print_clear();
	terminal.print_set_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK);
	terminal.print_str(Logo);

	Framebuffer::get_instance()->Draw(PotatoLogo, 0, 0, 197, 197);
	const auto c = static_cast<char *>(vmm.kmalloc(217));
	vmm.kfree(c);
	halt();
}