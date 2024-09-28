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
extern char getChar();
static BootInfo bi;
static IDT idt;
VMM vmm = nullptr;

void clear(pt::uintptr_t *ptr, const pt::size_t size) {
	for (pt::size_t i = 0; i < size; i++) {
		*ptr = 0;
	}
}

ASMCALL [[noreturn]] void kernel_main(boot_info* boot_info, void* l4_page_table) {
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
	terminal.print_set_color(PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
	char cmd[16] = {0};
	int pos = 0;
	while (true) {
		const char input_char = getChar();
		if (input_char == '\n') {
			klog("\n");
			if (cmd[0] == 'm' && cmd[1] == 'e' && cmd[2] == 'm' && cmd[3] == '\0') {
				klog("Free memory: %l\n", vmm.memsize());
			}
			else if (cmd[0] == 't' && cmd[1] == 'i' && cmd[2] == 'c' && cmd[3] == 'k' && cmd[4] == 's' && cmd[5] == '\0') {
				klog("Ticks: %l\n", get_ticks());
			}
			else if (cmd[0] == 'a' && cmd[1] == 'l' && cmd[2] == 'l' && cmd[3] == 'o' && cmd[4] == 'c' && cmd[5] == '\0') {
				const auto ptr = vmm.kcalloc(256);
				klog("Allocating 256 bytes: %x\n", ptr);
			}
			else {
				klog("Invalid command\n");
			}
			pos = 0;
			clear(reinterpret_cast<pt::uintptr_t*>(cmd), 16);
		}
		else if (input_char != -1) {
			cmd[pos++] = input_char;
			klog("%c", cmd[pos-1]);
		}
	}
}