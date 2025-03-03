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

constexpr char mem_cmd[] = "mem";
constexpr char ticks_cmd[] = "ticks";
constexpr char alloc_cmd[] = "alloc";
constexpr char clear_blue_cmd[] = "blue";
constexpr char clear_red_cmd[] = "red";
constexpr char clear_green_cmd[] = "green";
constexpr char quit_cmd[] = "quit";
constexpr char vmm_cmd[] = "vmm";
constexpr char pci_cmd[] = "pci";
constexpr char map_cmd[] = "map";

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

    Framebuffer::get_instance()->Draw(PotatoLogo, 0, 0, 197, 197);
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
            if (memcmp(cmd, mem_cmd, sizeof(mem_cmd))) {
                klog("Free memory: %l\n", vmm.memsize());
            }
            else if (memcmp(cmd, ticks_cmd, sizeof(ticks_cmd))) {
                klog("Ticks: %l\n", get_ticks());
            }
            else if (memcmp(cmd, alloc_cmd, sizeof(alloc_cmd))) {
                const auto ptr = vmm.kcalloc(256);
                klog("Allocating 256 bytes: %x\n", ptr);
                vmm.kfree(ptr);
            }
            else if (memcmp(cmd, clear_blue_cmd, sizeof(clear_blue_cmd))) {
                Framebuffer::get_instance()->Clear(0,0,255);
            }
            else if (memcmp(cmd, clear_green_cmd, sizeof(clear_green_cmd))) {
                Framebuffer::get_instance()->Clear(0,255,0);
            }
            else if (memcmp(cmd, clear_red_cmd, sizeof(clear_red_cmd))) {
                Framebuffer::get_instance()->Clear(255,0,0);
            }
            else if (memcmp(cmd, map_cmd, sizeof(map_cmd))) {
                // TODO: implement map command
            }
            else if (memcmp(cmd, vmm_cmd, sizeof(vmm_cmd))) {
                auto *pageTableL3 = reinterpret_cast<int *>(vmm.GetPageTableL3());
                klog("Paging struct at address: %x\n", pageTableL3);
                for (int i = 0; i < 1024; i+=4) {
                    if (*(pageTableL3 + i) != 0x0) {
                        klog("Page table entry for index %d: %x\n", i/4, *(pageTableL3 + i));
                    }
                }
            }
            else if (memcmp(cmd, pci_cmd, sizeof(pci_cmd))) {
                const auto devices = pci::enumerate();
                auto device = devices;
                while (device != nullptr && device->vendor_id != 0xffff)
                {
                    klog("PCI device: \n");
                    klog("\t\tVendorId: %x\n", device->vendor_id);
                    klog("\t\tDeviceId: %x\n", device->device_id);
                    klog("\t\tClass: %x\n", device->class_code);
                    klog("\t\tSubclass: %x\n", device->subclass_code);
                    device += sizeof(pci_device);
                }
                vmm.kfree(devices);
            }
            else if (memcmp(cmd, quit_cmd, sizeof(quit_cmd)))
            {
                klog("bye bye ;)\n");
                break;
            }
            else {
                klog("Invalid command\n");
            }
            pos = 0;
            clear(reinterpret_cast<pt::uintptr_t*>(cmd), 16);
        }
        else if (input_char != -1) {
            if (input_char == '\b')
            {
                cmd[--pos] = '\0';
                klog("\b \b");
            }
            else {
                cmd[pos++] = input_char;
                klog("%c", cmd[pos - 1]);
            }
        }
    }
    Framebuffer::get_instance()->Free();
    halt();
}