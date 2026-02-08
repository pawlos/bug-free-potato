#include "shell.h"
#include "kernel.h"
#include "print.h"
#include "framebuffer.h"
#include "virtual.h"
#include "timer.h"
#include "pci.h"
#include "./libs/stdlib.h"

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
constexpr char history_cmd[] = "history";

Shell::Shell() : history_count(0), history_index(0) {
    for (int i = 0; i < MAX_HISTORY; i++) {
        history[i][0] = '\0';
    }
}

void Shell::add_to_history(const char* cmd) {
    if (cmd[0] == '\0') return;
    
    // Check if same as last command
    if (history_count > 0) {
        int last_idx = (history_index - 1 + MAX_HISTORY) % MAX_HISTORY;
        bool same = true;
        for (int i = 0; i < CMD_BUFFER_SIZE; i++) {
            if (cmd[i] != history[last_idx][i]) {
                same = false;
                break;
            }
            if (cmd[i] == '\0') break;
        }
        if (same) return;
    }
    
    // Copy command to history
    int i = 0;
    while (i < CMD_BUFFER_SIZE - 1 && cmd[i] != '\0') {
        history[history_index][i] = cmd[i];
        i++;
    }
    history[history_index][i] = '\0';
    
    history_index = (history_index + 1) % MAX_HISTORY;
    if (history_count < MAX_HISTORY) {
        history_count++;
    }
}

void Shell::print_history() {
    klog("Command history:\n");
    int start = (history_index - history_count + MAX_HISTORY) % MAX_HISTORY;
    for (int i = 0; i < history_count; i++) {
        int idx = (start + i) % MAX_HISTORY;
        klog("  %d: %s\n", i + 1, history[idx]);
    }
}

bool Shell::execute(const char* cmd) {
    // Add to history first
    add_to_history(cmd);
    
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
            device++;
        }
        vmm.kfree(devices);
    }
    else if (memcmp(cmd, history_cmd, sizeof(history_cmd))) {
        print_history();
    }
    else if (memcmp(cmd, quit_cmd, sizeof(quit_cmd)))
    {
        klog("bye bye ;)\n");
        return false;
    }
    else {
        klog("Invalid command\n");
    }
    
    return true;
}

Shell shell;
