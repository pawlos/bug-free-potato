#include "shell.h"
#include "kernel.h"
#include "print.h"
#include "framebuffer.h"
#include "virtual.h"
#include "timer.h"
#include "pci.h"
#include "disk.h"
#include "fat12.h"
#include "ac97.h"
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
constexpr char ls_cmd[] = "ls";
constexpr char cat_cmd[] = "cat ";
constexpr char play_cmd[] = "play ";
constexpr char disk_cmd[] = "disk";
constexpr char help_cmd[] = "help";
constexpr char echo_cmd[] = "echo ";
constexpr char clear_cmd[] = "clear";
constexpr char timers_cmd[] = "timers";
constexpr char cancel_cmd[] = "cancel ";

void print_help() {
    klog("Available commands:\n");
    klog("  mem              - Display free memory\n");
    klog("  vmm              - Show page table entries\n");
    klog("  ticks            - Display system ticks\n");
    klog("  alloc [size]     - Allocate and free memory (default 256 bytes)\n");
    klog("  disk             - Show disk info\n");
    klog("  ls               - List files on disk\n");
    klog("  cat <filename>   - Read and display file\n");
    klog("  play <filename>  - Play audio file (AC97)\n");
    klog("  pci              - Enumerate PCI devices\n");
    klog("  blue/red/green   - Clear screen with color\n");
    klog("  echo <text>      - Print text to screen\n");
    klog("  clear            - Clear screen to black\n");
    klog("  timers           - List all active timers\n");
    klog("  cancel <id>      - Cancel a timer by ID\n");
    klog("  history          - Show command history\n");
    klog("  help             - Show this help\n");
    klog("  quit             - Exit kernel\n");
}

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
    else if (memcmp(cmd, "alloc", 5)) {
        // Parse size from command: "alloc" or "alloc <size>"
        const char* size_str = cmd + 5;  // Skip "alloc"
        pt::size_t size = parse_decimal(size_str);

        if (size == 0) {
            size = 256;  // Default to 256 if not specified or 0
        }

        const auto ptr = vmm.kcalloc(size);
        klog("Allocated %d bytes at %x\n", size, ptr);
        vmm.kfree(ptr);
        klog("Freed successfully\n");
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
    else if (memcmp(cmd, ls_cmd, sizeof(ls_cmd))) {
        FAT12::list_root_directory();
    }
    else if (memcmp(cmd, disk_cmd, sizeof(disk_cmd))) {
        if (Disk::is_present()) {
            klog("Disk present: %d sectors (%d MB)\n",
                 Disk::get_sector_count(),
                 (Disk::get_sector_count() * 512) / (1024 * 1024));
        } else {
            klog("No disk present\n");
        }
    }
    else if (memcmp(cmd, play_cmd, 5)) {
        const char* filename = cmd + 5;
        if (filename[0] == '\0') {
            klog("Usage: play <filename>\n");
        } else if (!AC97::is_present()) {
            klog("AC97 audio not available\n");
        } else {
            FAT12_File file;
            if (FAT12::open_file(filename, &file)) {
                klog("Playing: %s (%d bytes)\n", file.filename, file.file_size);
                pt::uint8_t* buf = (pt::uint8_t*)vmm.kmalloc(file.file_size);
                if (buf) {
                    pt::uint32_t bytes_read = FAT12::read_file(&file, buf, file.file_size);
                    AC97::play_pcm(buf, bytes_read, 48000);
                    // Wait for DMA to finish before freeing the buffer
                    while (AC97::is_playing()) { /* spin */ }
                    vmm.kfree(buf);
                }
                FAT12::close_file(&file);
            } else {
                klog("File not found: %s\n", filename);
            }
        }
    }
    else if (memcmp(cmd, cat_cmd, 4)) {
        // Cat command - cmd starts with "cat "
        const char* filename = cmd + 4; // Skip "cat "
        if (filename[0] == '\0') {
            klog("Usage: cat <filename>\n");
        } else {
            FAT12_File file;
            if (FAT12::open_file(filename, &file)) {
                klog("File: %s (%d bytes)\n", file.filename, file.file_size);
                char* buffer = (char*)vmm.kmalloc(file.file_size + 1);
                if (buffer) {
                    pt::uint32_t bytes_read = FAT12::read_file(&file, buffer, file.file_size);
                    buffer[bytes_read] = '\0';
                    klog("Contents:\n%s\n", buffer);
                    vmm.kfree(buffer);
                }
                FAT12::close_file(&file);
            } else {
                klog("File not found: %s\n", filename);
            }
        }
    }
    else if (memcmp(cmd, help_cmd, sizeof(help_cmd))) {
        print_help();
    }
    else if (memcmp(cmd, echo_cmd, 5)) {
        // Echo command - print everything after "echo "
        const char* text = cmd + 5;
        klog("%s\n", text);
    }
    else if (memcmp(cmd, clear_cmd, sizeof(clear_cmd))) {
        // Clear command - clear the screen to black
        Framebuffer::get_instance()->Clear(0, 0, 0);
    }
    else if (memcmp(cmd, timers_cmd, sizeof(timers_cmd))) {
        // Timers command - list all active timers
        timer_list_all();
    }
    else if (memcmp(cmd, cancel_cmd, 7)) {
        // Cancel command - cancel a timer by ID
        const char* id_str = cmd + 7;  // Skip "cancel "
        pt::uint64_t timer_id = parse_decimal(id_str);
        if (timer_id == 0) {
            klog("Usage: cancel <timer_id>\n");
        } else {
            timer_cancel(timer_id);
        }
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
