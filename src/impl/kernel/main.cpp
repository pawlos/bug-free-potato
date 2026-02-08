#include "./libs/stdlib.h"
#include "print.h"
#include "com.h"
#include "boot.h"
#include "idt.h"
#include "kernel.h"
#include "framebuffer.h"
#include "virtual.h"
#include "timer.h"
#include "mouse.h"
#include "pci.h"
#include "shell.h"
#include "line_reader.h"

extern const char Logo[];
extern const unsigned char PotatoLogo[];
extern char get_char();
static IDT idt;
VMM vmm;

void* operator new(std::size_t n) {
    return vmm.kcalloc(n);
}

void* operator new[](std::size_t n) {
    return vmm.kcalloc(n);
}

void operator delete(void* p) {
    vmm.kfree(p);
}

void operator delete[](void* p) {
    vmm.kfree(p);
}

pt::uint64_t old_ticks = 0;

ASMCALL void kernel_main(boot_info* boot_info, void* l4_page_table) {
    klog("[MAIN] Welcome to 64-bit potat OS\n");

    auto bi = BootInfo(boot_info);

    const auto boot_fb = bi.get_framebuffer();

    init_mouse(boot_fb->framebuffer_width, boot_fb->framebuffer_height);
    init_timer(50);

    idt.initialize();

    vmm = VMM(bi.get_memory_maps(), l4_page_table);

    Framebuffer::Init(boot_fb);

    TerminalPrinter terminal;

    terminal.print_clear();
    terminal.print_set_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK);
    terminal.print_str(Logo);

    const pt::uint32_t img_width = 197;
    const pt::uint32_t img_height = 197;
    const pt::uint32_t center_x = (boot_fb->framebuffer_width - img_width) / 2;
    const pt::uint32_t center_y = (boot_fb->framebuffer_height - img_height) / 2;
    Framebuffer::get_instance()->Draw(PotatoLogo, center_x, center_y, img_width, img_height);
    const auto c = static_cast<char *>(vmm.kmalloc(217));
    vmm.kfree(c);
    terminal.print_set_color(PRINT_COLOR_WHITE, PRINT_COLOR_BLACK);
    LineReader reader;
    while (true) {
        reader.process(get_char());

        if (reader.has_line()) {
            const char* line = reader.get_line();
            if (line[0] != '\0') {
                if (!shell.execute(line)) {
                    break;
                }
            }
            reader.clear();
        }

        pt::uint64_t temp_ticks = get_ticks();
        if (temp_ticks - old_ticks > 50)
        {
            old_ticks = temp_ticks;
            klog("Seconds: %d\n", old_ticks/50);
        }
    }
    Framebuffer::get_instance()->Free();
    halt();
}