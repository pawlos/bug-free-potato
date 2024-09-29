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

bool memcmp(const void *src, const void *dst, const pt::size_t size) {
	for (pt::size_t i = 0; i < size; i++) {
		const char src_char = *static_cast<const char *>(src);
		if (const char dst_char = *static_cast<const char *>(dst); src_char != dst_char) {
			return false;
		}
		src++;
		dst++;
	}
	return true;
}

void* operator new(std::size_t n) {
	return vmm.kcalloc(n);
}

void* operator new[](std::size_t n) {
	return vmm.kcalloc(n);
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
	const char mem_cmd[] = "mem";
	const char ticks_cmd[] = "ticks";
	const char alloc_cmd[] = "alloc";
	while (true) {
		const char input_char = getChar();
		if (input_char == '\n') {
			klog("\n");
			if (memcmp(cmd, mem_cmd, 3)) {
				klog("Free memory: %l\n", vmm.memsize());
			}
			else if (memcmp(cmd, ticks_cmd, 5)) {
				klog("Ticks: %l\n", get_ticks());
			}
			else if (memcmp(cmd, alloc_cmd, 5)) {
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