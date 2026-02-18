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
#include "acpi.h"
#include "task.h"
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
constexpr char shutdown_cmd[] = "shutdown";
constexpr char reboot_cmd[] = "reboot";
constexpr char task_cmd[] = "task";

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
    klog("  map [test]       - Show page table or test dynamic mapping (test)\n");
    klog("  pci              - Enumerate PCI devices\n");
    klog("  blue/red/green   - Clear screen with color\n");
    klog("  echo <text>      - Print text to screen\n");
    klog("  clear            - Clear screen to black\n");
    klog("  timers           - List all active timers\n");
    klog("  cancel <id>      - Cancel a timer by ID\n");
    klog("  history          - Show command history\n");
    klog("  shutdown         - Shutdown system (ACPI or PS/2)\n");
    klog("  reboot           - Reboot system\n");
    klog("  task [create]    - Show tasks or create test task\n");
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

void Shell::execute_mem(const char* cmd) {
    klog("Free memory: %l\n", vmm.memsize());
}

void Shell::execute_ticks(const char* cmd) {
    klog("Ticks: %l\n", get_ticks());
}

void Shell::execute_alloc(const char* cmd) {
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

void Shell::execute_clear_color(const char* cmd, pt::uint8_t r, pt::uint8_t g, pt::uint8_t b) {
    Framebuffer::get_instance()->Clear(r, g, b);
}

void Shell::execute_vmm(const char* cmd) {
    auto *pageTableL3 = reinterpret_cast<int *>(vmm.GetPageTableL3());
    klog("Paging struct at address: %x\n", pageTableL3);
    for (int i = 0; i < 1024; i+=4) {
        if (*(pageTableL3 + i) != 0x0) {
            klog("Page table entry for index %d: %x\n", i/4, *(pageTableL3 + i));
        }
    }
}

void Shell::execute_pci(const char* cmd) {
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

void Shell::execute_map(const char* cmd) {
    // Parse command: "map" to show current mappings, "map test" to test allocation
    const char* args = cmd + 3;  // Skip "map"

    // Skip whitespace
    while (*args == ' ' || *args == '\t') args++;

    if (*args == '\0')
    {
        // Display current page table mappings
        auto *pageTableL3 = reinterpret_cast<int *>(vmm.GetPageTableL3());
        klog("Page table root at %x\n", pageTableL3);
        for (int i = 0; i < 10; i++)
        {
            if (*(pageTableL3 + i) != 0x0)
            {
                klog("  L4[%d]: %x\n", i, *(pageTableL3 + i));
            }
        }
        return;
    }

    // Check for "map test" command
    if (args[0] == 't' && args[1] == 'e' && args[2] == 's' && args[3] == 't')
    {
        klog("[MAP TEST] Testing dynamic page allocation and mapping...\n");

        // Test 1: Allocate some pages
        klog("[MAP TEST] Test 1: Allocating and mapping high virtual address...\n");
        pt::uintptr_t test_virt = 0x10000000;  // High virtual address
        pt::uintptr_t test_phys = vmm.allocate_frame();  // Get physical frame
        klog("[MAP TEST] Got physical frame at %x\n", test_phys);

        // Map the page
        vmm.map_page(test_virt, test_phys, 0x03);  // RW flags
        klog("[MAP TEST] Mapped virt %x to phys %x\n", test_virt, test_phys);

        // Verify the mapping
        pt::uintptr_t verify = vmm.virt_to_phys_walk(test_virt);
        if (verify == test_phys)
        {
            klog("[MAP TEST] [OK] Verification passed: %x -> %x\n", test_virt, verify);
        }
        else
        {
            klog("[MAP TEST] [FAIL] Verification FAILED: expected %x, got %x\n", test_phys, verify);
        }

        // Test 2: Multiple allocations
        klog("[MAP TEST] Test 2: Allocating multiple frames...\n");
        pt::uintptr_t frames[5];
        for (int i = 0; i < 5; i++)
        {
            frames[i] = vmm.allocate_frame();
            klog("[MAP TEST]   Frame %d: %x\n", i, frames[i]);
        }

        klog("[MAP TEST] All tests completed!\n");
        return;
    }

    // Query mapping: "map <virt_addr>"
    pt::uintptr_t virt = 0;
    const char* ptr = args;
    while (*ptr >= '0' && *ptr <= '9') {
        virt = virt * 10 + (*ptr - '0');
        ptr++;
    }

    pt::uintptr_t phys = vmm.virt_to_phys_walk(virt);
    if (phys != 0)
    {
        klog("Virt %x -> Phys %x\n", virt, phys);
    }
    else
    {
        klog("Virt %x not mapped\n", virt);
    }
}

void Shell::execute_history(const char* cmd) {
    print_history();
}

void Shell::execute_ls(const char* cmd) {
    FAT12::list_root_directory();
}

void Shell::execute_disk(const char* cmd) {
    if (Disk::is_present()) {
        klog("Disk present: %d sectors (%d MB)\n",
             Disk::get_sector_count(),
             (Disk::get_sector_count() * 512) / (1024 * 1024));
    } else {
        klog("No disk present\n");
    }
}

void Shell::execute_play(const char* cmd) {
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

void Shell::execute_cat(const char* cmd) {
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

void Shell::execute_shutdown(const char* cmd) {
    klog("Shutting down...\n");
    ACPI::shutdown();
    // If we reach here, the system didn't shut down
    klog("Shutdown failed\n");
}

void Shell::execute_reboot(const char* cmd) {
    klog("Rebooting...\n");
    ACPI::reboot();
    // If we reach here, the system didn't reboot
    klog("Reboot failed\n");
}

// Simple test task that prints a few messages then exits
void test_task_fn() {
    klog("[TASK] Test task running!\n");
    for (int i = 0; i < 5; i++) {
        klog("[TASK] Test task iteration %d\n", i);
        TaskScheduler::task_yield();
    }
    klog("[TASK] Test task exiting\n");
    TaskScheduler::task_exit();
}

void Shell::execute_task(const char* cmd) {
    const char* args = cmd + 4;  // Skip "task"

    // Skip whitespace
    while (*args == ' ' || *args == '\t') args++;

    if (*args == '\0')
    {
        // List all tasks
        klog("[SHELL] Task list:\n");
        Task* current = TaskScheduler::get_current_task();
        klog("  Current task ID: %d\n", current->id);
        klog("  Task states:\n");
        for (int i = 0; i < 16; i++) {
            // We can't directly access tasks array, so just show current
            klog("    Running task %d (ticks alive: %d)\n", current->id, (int)current->ticks_alive);
        }
        return;
    }

    // Check for "task create" command
    if (args[0] == 'c' && args[1] == 'r' && args[2] == 'e' && args[3] == 'a' && args[4] == 't' && args[5] == 'e')
    {
        klog("[SHELL] Creating test task...\n");
        pt::uint32_t task_id = TaskScheduler::create_task(&test_task_fn);
        if (task_id != 0xFFFFFFFF)
        {
            klog("[SHELL] Created task with ID %d\n", task_id);
        }
        else
        {
            klog("[SHELL] Failed to create task\n");
        }
        return;
    }

    klog("Usage: task [create]\n");
}

void Shell::execute_help(const char* cmd) {
    print_help();
}

void Shell::execute_echo(const char* cmd) {
    // Echo command - print everything after "echo "
    const char* text = cmd + 5;
    klog("%s\n", text);
}

void Shell::execute_clear(const char* cmd) {
    // Clear command - clear the screen to black
    Framebuffer::get_instance()->Clear(0, 0, 0);
}

void Shell::execute_timers(const char* cmd) {
    // Timers command - list all active timers
    timer_list_all();
}

void Shell::execute_cancel(const char* cmd) {
    // Cancel command - cancel a timer by ID
    const char* id_str = cmd + 7;  // Skip "cancel "
    pt::uint64_t timer_id = parse_decimal(id_str);
    if (timer_id == 0) {
        klog("Usage: cancel <timer_id>\n");
    } else {
        timer_cancel(timer_id);
    }
}

bool Shell::execute(const char* cmd) {
    // Add to history first
    add_to_history(cmd);

    if (memcmp(cmd, mem_cmd, sizeof(mem_cmd))) {
        execute_mem(cmd);
    }
    else if (memcmp(cmd, ticks_cmd, sizeof(ticks_cmd))) {
        execute_ticks(cmd);
    }
    else if (memcmp(cmd, "alloc", 5)) {
        execute_alloc(cmd);
    }
    else if (memcmp(cmd, clear_blue_cmd, sizeof(clear_blue_cmd))) {
        execute_clear_color(cmd, 0, 0, 255);
    }
    else if (memcmp(cmd, clear_green_cmd, sizeof(clear_green_cmd))) {
        execute_clear_color(cmd, 0, 255, 0);
    }
    else if (memcmp(cmd, clear_red_cmd, sizeof(clear_red_cmd))) {
        execute_clear_color(cmd, 255, 0, 0);
    }
    else if (memcmp(cmd, map_cmd, 3)) {  // "map" is 3 chars, don't include null terminator
        execute_map(cmd);
    }
    else if (memcmp(cmd, vmm_cmd, sizeof(vmm_cmd))) {
        execute_vmm(cmd);
    }
    else if (memcmp(cmd, pci_cmd, sizeof(pci_cmd))) {
        execute_pci(cmd);
    }
    else if (memcmp(cmd, history_cmd, sizeof(history_cmd))) {
        execute_history(cmd);
    }
    else if (memcmp(cmd, ls_cmd, sizeof(ls_cmd))) {
        execute_ls(cmd);
    }
    else if (memcmp(cmd, disk_cmd, sizeof(disk_cmd))) {
        execute_disk(cmd);
    }
    else if (memcmp(cmd, play_cmd, 5)) {
        execute_play(cmd);
    }
    else if (memcmp(cmd, cat_cmd, 4)) {
        execute_cat(cmd);
    }
    else if (memcmp(cmd, help_cmd, sizeof(help_cmd))) {
        execute_help(cmd);
    }
    else if (memcmp(cmd, echo_cmd, 5)) {
        execute_echo(cmd);
    }
    else if (memcmp(cmd, clear_cmd, sizeof(clear_cmd))) {
        execute_clear(cmd);
    }
    else if (memcmp(cmd, timers_cmd, sizeof(timers_cmd))) {
        execute_timers(cmd);
    }
    else if (memcmp(cmd, cancel_cmd, 7)) {
        execute_cancel(cmd);
    }
    else if (memcmp(cmd, shutdown_cmd, sizeof(shutdown_cmd))) {
        execute_shutdown(cmd);
    }
    else if (memcmp(cmd, reboot_cmd, sizeof(reboot_cmd))) {
        execute_reboot(cmd);
    }
    else if (memcmp(cmd, task_cmd, 4)) {
        execute_task(cmd);
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
