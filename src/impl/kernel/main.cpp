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
    char cmd[16] = {0};
    int pos = 0;
    while (true) {
        const char input_char = get_char();
        if (input_char == '\n') {
            klog("\n");
            if (cmd[0] == '\0') {
                continue;
            }
            if (!shell.execute(cmd)) {
                break;
            }
            pos = 0;
            clear(reinterpret_cast<pt::uintptr_t*>(cmd), 16);
        }
        else if (input_char != -1) {
            if (input_char == '\b' && pos > 0)
            {
                cmd[--pos] = '\0';
                klog("\b \b");
            }
            else if (pos < 15) {
                cmd[pos++] = input_char;
                cmd[pos] = '\0';
                klog("%c", cmd[pos - 1]);
            }
        }
    }
    Framebuffer::get_instance()->Free();
    halt();
}