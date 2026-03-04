#include "shell.h"
#include "kernel.h"
#include "vterm.h"
#include "elf_loader.h"
#include "print.h"
#include "framebuffer.h"
#include "virtual.h"
#include "device/timer.h"
#include "device/pci.h"
#include "device/disk.h"
#include "fs/vfs.h"
#include "device/ac97.h"
#include "device/acpi.h"
#include "task.h"
#include "net/net.h"
#include "libs/stdlib.h"

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
constexpr char write_cmd[] = "write ";
constexpr char rm_cmd[] = "rm ";
constexpr char exec_cmd[] = "exec ";
constexpr char net_cmd[]      = "net";
constexpr char ping_cmd[]     = "ping ";
constexpr char dhcp_cmd[]     = "dhcp";
constexpr char nslookup_cmd[] = "nslookup";
constexpr char wget_cmd[]     = "wget";

void print_help() {
    vterm_printf("Available commands:\n");
    vterm_printf("  mem              - Display free memory\n");
    vterm_printf("  vmm              - Show page table entries\n");
    vterm_printf("  ticks            - Display system ticks\n");
    vterm_printf("  alloc [size]     - Allocate and free memory (default 256 bytes)\n");
    vterm_printf("  disk             - Show disk info\n");
    vterm_printf("  ls               - List files on disk\n");
    vterm_printf("  cat <filename>   - Read and display file\n");
    vterm_printf("  play <filename>  - Play audio file (AC97)\n");
    vterm_printf("  map [test]       - Show page table or test dynamic mapping (test)\n");
    vterm_printf("  pci              - Enumerate PCI devices\n");
    vterm_printf("  blue/red/green   - Clear screen with color\n");
    vterm_printf("  echo <text>      - Print text to screen\n");
    vterm_printf("  clear            - Clear screen to black\n");
    vterm_printf("  timers           - List all active timers\n");
    vterm_printf("  cancel <id>      - Cancel a timer by ID\n");
    vterm_printf("  history          - Show command history\n");
    vterm_printf("  shutdown         - Shutdown system (ACPI or PS/2)\n");
    vterm_printf("  reboot           - Reboot system\n");
    vterm_printf("  task [create|map] - Show tasks, create test task, or dump memory map\n");
    vterm_printf("  write <file> <text> - Create a new file with text content\n");
    vterm_printf("  rm <file>        - Delete a file\n");
    vterm_printf("  exec <file>      - Load and run an ELF program\n");
    vterm_printf("  net              - Show network configuration\n");
    vterm_printf("  ping <ip|host>   - Send ICMP echo requests (resolves hostname via DNS)\n");
    vterm_printf("  dhcp             - Run DHCP to acquire IP address\n");
    vterm_printf("  nslookup <host>  - Resolve hostname via DNS\n");
    vterm_printf("  wget <host> [path] - HTTP/1.0 GET request via TCP\n");
    vterm_printf("  help             - Show this help\n");
    vterm_printf("  quit             - Exit kernel\n");
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
    vterm_printf("Command history:\n");
    int start = (history_index - history_count + MAX_HISTORY) % MAX_HISTORY;
    for (int i = 0; i < history_count; i++) {
        int idx = (start + i) % MAX_HISTORY;
        vterm_printf("  %d: %s\n", i + 1, history[idx]);
    }
}

void Shell::execute_mem(const char* cmd) {
    vterm_printf("Free memory: %l\n", vmm.memsize());
}

void Shell::execute_ticks(const char* cmd) {
    vterm_printf("Ticks: %l\n", get_ticks());
}

void Shell::execute_alloc(const char* cmd) {
    // Parse size from command: "alloc" or "alloc <size>"
    const char* size_str = cmd + 5;  // Skip "alloc"
    pt::size_t size = parse_decimal(size_str);

    if (size == 0) {
        size = 256;  // Default to 256 if not specified or 0
    }

    const auto ptr = vmm.kcalloc(size);
    vterm_printf("Allocated %d bytes at %x\n", size, ptr);
    vmm.kfree(ptr);
    vterm_printf("Freed successfully\n");
}

void Shell::execute_clear_color(const char* cmd, pt::uint8_t r, pt::uint8_t g, pt::uint8_t b) {
    Framebuffer::get_instance()->Clear(r, g, b);
}

void Shell::execute_vmm(const char* cmd) {
    auto *pml4 = reinterpret_cast<pt::uint64_t *>(vmm.GetPageTableL3());
    vterm_printf("Paging struct at address: %x\n", pml4);
    for (int i = 0; i < 512; i++) {
        if (pml4[i] != 0) {
            vterm_printf("Page table entry for index %d: %x\n", i, pml4[i]);
        }
    }
}

void Shell::execute_pci(const char* cmd) {
    const auto devices = pci::enumerate();
    auto device = devices;
    while (device != nullptr && device->vendor_id != 0xffff)
    {
        vterm_printf("PCI device: \n");
        vterm_printf("\t\tVendorId: %x\n", device->vendor_id);
        vterm_printf("\t\tDeviceId: %x\n", device->device_id);
        vterm_printf("\t\tClass: %x\n", device->class_code);
        vterm_printf("\t\tSubclass: %x\n", device->subclass_code);
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
        vterm_printf("Page table root at %x\n", pageTableL3);
        for (int i = 0; i < 10; i++)
        {
            if (*(pageTableL3 + i) != 0x0)
            {
                vterm_printf("  L4[%d]: %x\n", i, *(pageTableL3 + i));
            }
        }
        return;
    }

    // Check for "map test" command
    if (args[0] == 't' && args[1] == 'e' && args[2] == 's' && args[3] == 't')
    {
        vterm_printf("[MAP TEST] Testing dynamic page allocation and mapping...\n");

        // Test 1: Allocate some pages
        vterm_printf("[MAP TEST] Test 1: Allocating and mapping high virtual address...\n");
        pt::uintptr_t test_virt = 0x10000000;  // High virtual address
        pt::uintptr_t test_phys = vmm.allocate_frame();  // Get physical frame
        vterm_printf("[MAP TEST] Got physical frame at %x\n", test_phys);

        // Map the page
        vmm.map_page(test_virt, test_phys, 0x03);  // RW flags
        vterm_printf("[MAP TEST] Mapped virt %x to phys %x\n", test_virt, test_phys);

        // Verify the mapping
        pt::uintptr_t verify = vmm.virt_to_phys_walk(test_virt);
        if (verify == test_phys)
        {
            vterm_printf("[MAP TEST] [OK] Verification passed: %x -> %x\n", test_virt, verify);
        }
        else
        {
            vterm_printf("[MAP TEST] [FAIL] Verification FAILED: expected %x, got %x\n", test_phys, verify);
        }

        // Test 2: Multiple allocations
        vterm_printf("[MAP TEST] Test 2: Allocating multiple frames...\n");
        pt::uintptr_t frames[5];
        for (int i = 0; i < 5; i++)
        {
            frames[i] = vmm.allocate_frame();
            vterm_printf("[MAP TEST]   Frame %d: %x\n", i, frames[i]);
        }

        vterm_printf("[MAP TEST] All tests completed!\n");
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
        vterm_printf("Virt %x -> Phys %x\n", virt, phys);
    }
    else
    {
        vterm_printf("Virt %x not mapped\n", virt);
    }
}

void Shell::execute_history(const char* cmd) {
    print_history();
}

void Shell::execute_ls(const char* cmd) {
    VFS::list_root_directory();
}

void Shell::execute_disk(const char* cmd) {
    if (Disk::is_present()) {
        vterm_printf("Disk present: %d sectors (%d MB)\n",
             Disk::get_sector_count(),
             (Disk::get_sector_count() * 512) / (1024 * 1024));
    } else {
        vterm_printf("No disk present\n");
    }
}

void Shell::execute_play(const char* cmd) {
    const char* filename = cmd + 5;
    if (filename[0] == '\0') {
        vterm_printf("Usage: play <filename>\n");
    } else if (!AC97::is_present()) {
        vterm_printf("AC97 audio not available\n");
    } else {
        File file;
        if (VFS::open_file(filename, &file)) {
            vterm_printf("Playing: %s (%d bytes)\n", file.filename, file.file_size);
            pt::uint8_t* buf = (pt::uint8_t*)vmm.kmalloc(file.file_size);
            if (buf) {
                pt::uint32_t bytes_read = VFS::read_file(&file, buf, file.file_size);
                AC97::play_pcm(buf, bytes_read, 48000);
                // Wait for DMA to finish before freeing the buffer
                while (AC97::is_playing()) { /* spin */ }
                vmm.kfree(buf);
            }
            VFS::close_file(&file);
        } else {
            vterm_printf("File not found: %s\n", filename);
        }
    }
}

void Shell::execute_cat(const char* cmd) {
    // Cat command - cmd starts with "cat "
    const char* filename = cmd + 4; // Skip "cat "
    if (filename[0] == '\0') {
        vterm_printf("Usage: cat <filename>\n");
    } else {
        File file;
        if (VFS::open_file(filename, &file)) {
            vterm_printf("File: %s (%d bytes)\n", file.filename, file.file_size);
            char* buffer = (char*)vmm.kmalloc(file.file_size + 1);
            if (buffer) {
                pt::uint32_t bytes_read = VFS::read_file(&file, buffer, file.file_size);
                buffer[bytes_read] = '\0';
                vterm_printf("Contents:\n%s\n", buffer);
                vmm.kfree(buffer);
            }
            VFS::close_file(&file);
        } else {
            vterm_printf("File not found: %s\n", filename);
        }
    }
}

void Shell::execute_shutdown(const char* cmd) {
    vterm_printf("Shutting down...\n");
    ACPI::shutdown();
    // If we reach here, the system didn't shut down
    vterm_printf("Shutdown failed\n");
}

void Shell::execute_reboot(const char* cmd) {
    vterm_printf("Rebooting...\n");
    ACPI::reboot();
    // If we reach here, the system didn't reboot
    vterm_printf("Reboot failed\n");
}

// Simple test task that prints a few messages then exits
void test_task_fn() {
    vterm_printf("[TASK] Test task running!\n");
    for (int i = 0; i < 5; i++) {
        vterm_printf("[TASK] Test task iteration %d\n", i);
        TaskScheduler::task_yield();
    }
    vterm_printf("[TASK] Test task exiting\n");
    TaskScheduler::task_exit();
}

void Shell::execute_task(const char* cmd) {
    const char* args = cmd + 4;  // Skip "task"

    // Skip whitespace
    while (*args == ' ' || *args == '\t') args++;

    if (*args == '\0')
    {
        // List all tasks
        Task* current = TaskScheduler::get_current_task();
        vterm_printf("  Current task ID: %d\n", current->id);
        return;
    }

    // Check for "task create" command
    if (args[0] == 'c' && args[1] == 'r' && args[2] == 'e' && args[3] == 'a' && args[4] == 't' && args[5] == 'e')
    {
        vterm_printf("[SHELL] Creating test task...\n");
        pt::uint32_t task_id = TaskScheduler::create_task(&test_task_fn);
        if (task_id != 0xFFFFFFFF)
            vterm_printf("[SHELL] Created task with ID %d\n", task_id);
        else
            vterm_printf("[SHELL] Failed to create task\n");
        return;
    }

    // "task map [id]" — dump memory map for a task
    if (args[0] == 'm' && args[1] == 'a' && args[2] == 'p') {
        const char* id_str = args + 3;
        while (*id_str == ' ' || *id_str == '\t') id_str++;
        if (*id_str == '\0') {
            // Dump all non-dead tasks.
            for (pt::uint32_t i = 0; i < TaskScheduler::MAX_TASKS; i++)
                TaskScheduler::dump_task_map(i);
        } else {
            // Parse decimal task id.
            pt::uint32_t tid = 0;
            while (*id_str >= '0' && *id_str <= '9') {
                tid = tid * 10 + (*id_str - '0');
                id_str++;
            }
            TaskScheduler::dump_task_map(tid);
        }
        return;
    }

    vterm_printf("Usage: task [create|map [id]]\n");
}

void Shell::execute_help(const char* cmd) {
    print_help();
}

void Shell::execute_write(const char* cmd) {
    // Skip "write " prefix (6 chars)
    const char* args = cmd + 6;

    // Parse filename (first whitespace-delimited token, max 12 chars)
    char filename[13];
    int fn_len = 0;
    while (args[fn_len] && args[fn_len] != ' ' && fn_len < 12) {
        filename[fn_len] = args[fn_len];
        fn_len++;
    }
    filename[fn_len] = '\0';

    if (fn_len == 0) {
        vterm_printf("Usage: write <filename> <content>\n");
        return;
    }

    // Content is everything after the filename and space
    const char* content = args + fn_len;
    if (*content == ' ') content++;

    pt::uint32_t len = 0;
    while (content[len]) len++;

    if (VFS::create_file(filename, (const pt::uint8_t*)content, len)) {
        vterm_printf("Created file '%s' (%d bytes)\n", filename, len);
    } else {
        if (VFS::file_exists(filename)) {
            vterm_printf("Error: file already exists\n");
        } else {
            vterm_printf("Error: disk full or write failed\n");
        }
    }
}




void Shell::execute_exec(const char* cmd) {
    const char* filename = cmd + 5;  // skip "exec "
    if (filename[0] == '\0') {
        vterm_printf("Usage: exec <filename>\n");
        return;
    }
    pt::uint32_t task_id = TaskScheduler::create_elf_task(filename);
    if (task_id == 0xFFFFFFFF)
        vterm_printf("exec: failed to start '%s'\n", filename);
    else
        vterm_printf("exec: started '%s' as task %d\n", filename, task_id);
}

void Shell::execute_rm(const char* cmd) {
    const char* filename = cmd + 3;  // Skip "rm "
    if (filename[0] == '\0') {
        vterm_printf("Usage: rm <filename>\n");
        return;
    }
    if (VFS::delete_file(filename)) {
        vterm_printf("Deleted '%s'\n", filename);
    } else {
        vterm_printf("Error: file not found or delete failed\n");
    }
}

void Shell::execute_echo(const char* cmd) {
    // Echo command - print everything after "echo "
    const char* text = cmd + 5;
    vterm_printf("%s\n", text);
}

// Helper: print a 32-bit IP in network byte order as dotted decimal
static void print_ip(pt::uint32_t ip) {
    const pt::uint8_t* b = reinterpret_cast<const pt::uint8_t*>(&ip);
    vterm_printf("%d.%d.%d.%d", (int)b[0], (int)b[1], (int)b[2], (int)b[3]);
}

// Helper: parse dotted-decimal IP string into network-byte-order uint32_t.
// Returns 0 on failure.
static pt::uint32_t parse_ip(const char* s) {
    pt::uint32_t octets[4] = {0, 0, 0, 0};
    int part = 0;
    for (; *s && part < 4; s++) {
        if (*s >= '0' && *s <= '9') {
            octets[part] = octets[part] * 10 + (pt::uint32_t)(*s - '0');
        } else if (*s == '.') {
            part++;
        } else {
            break;
        }
    }
    if (part != 3) return 0;
    // Build uint32_t in network byte order: octet[0] at lowest address
    return (octets[3] << 24) | (octets[2] << 16) | (octets[1] << 8) | octets[0];
}

void Shell::execute_net(const char* cmd) {
    (void)cmd;
    if (!RTL8139::is_present()) {
        vterm_printf("RTL8139 not present\n");
        return;
    }
    pt::uint8_t mac[6];
    RTL8139::get_mac(mac);
    vterm_printf("Net: ");
    print_ip(g_my_ip);
    vterm_printf(" / ");
    print_ip(make_ip(255, 255, 255, 0));
    vterm_printf("  gw ");
    print_ip(g_gateway_ip);
    vterm_printf("\n");
    vterm_printf("DNS: ");
    print_ip(g_dns_ip);
    vterm_printf("\n");
    vterm_printf("MAC: %x:%x:%x:%x:%x:%x\n",
         (int)mac[0], (int)mac[1], (int)mac[2],
         (int)mac[3], (int)mac[4], (int)mac[5]);
    vterm_printf("RTL8139 @ io=0x%x  IRQ=11\n", (pt::uint32_t)RTL8139::get_io_base());
}

void Shell::execute_ping(const char* cmd) {
    // Skip "ping" then any spaces
    const char* arg = cmd + 4;
    while (*arg == ' ') arg++;
    if (*arg == '\0') {
        vterm_printf("Usage: ping <ip|hostname>\n");
        return;
    }
    if (!RTL8139::is_present()) {
        vterm_printf("RTL8139 not present\n");
        return;
    }

    pt::uint32_t dst = parse_ip(arg);
    if (dst == 0) {
        // Try DNS resolution
        vterm_printf("Resolving %s ...\n", arg);
        if (!dns_resolve(arg, 100, dst) || dst == 0) {
            vterm_printf("Unknown host: %s\n", arg);
            return;
        }
    }

    vterm_printf("PING ");
    print_ip(dst);
    vterm_printf("\n");

    for (pt::uint16_t seq = 1; seq <= 4; seq++) {
        pt::uint64_t t0 = get_ticks();
        icmp_ping(dst, seq);
        if (icmp_wait_reply(seq, 100)) {
            pt::uint64_t rtt = icmp_last_reply_tick() - t0;
            vterm_printf("64 bytes from ");
            print_ip(dst);
            vterm_printf(": seq=%d time=%d ticks\n", (int)seq, (int)rtt);
        } else if (icmp_last_unreachable_seq() == seq) {
            vterm_printf("Destination unreachable: seq=%d\n", (int)seq);
        } else {
            vterm_printf("Request timeout for seq %d\n", (int)seq);
        }
    }
}

void Shell::execute_dhcp(const char* cmd) {
    (void)cmd;
    if (!RTL8139::is_present()) {
        vterm_printf("RTL8139 not present\n");
        return;
    }
    vterm_printf("Running DHCP...\n");
    if (dhcp_acquire(250)) {
        vterm_printf("DHCP OK: ");
        print_ip(g_my_ip);
        vterm_printf("\n");
    } else {
        vterm_printf("DHCP failed\n");
    }
}

void Shell::execute_nslookup(const char* cmd) {
    const char* host = cmd + 8; // skip "nslookup"
    while (*host == ' ') host++;
    if (*host == '\0') {
        vterm_printf("Usage: nslookup <hostname>\n");
        return;
    }
    if (!RTL8139::is_present()) {
        vterm_printf("RTL8139 not present\n");
        return;
    }
    vterm_printf("Resolving %s ...\n", host);
    pt::uint32_t ip = 0;
    if (dns_resolve(host, 100, ip)) {
        vterm_printf("%s -> ", host);
        print_ip(ip);
        vterm_printf("\n");
    } else {
        vterm_printf("No answer for %s\n", host);
    }
}

void Shell::execute_wget(const char* cmd) {
    const char* arg = cmd + 4;  // skip "wget"
    while (*arg == ' ') arg++;
    if (*arg == '\0') {
        vterm_printf("Usage: wget <host> [path]\n");
        return;
    }
    if (!RTL8139::is_present()) {
        vterm_printf("RTL8139 not present\n");
        return;
    }

    // Parse host (first whitespace-delimited token)
    char host[64];
    int hi = 0;
    while (*arg && *arg != ' ' && hi < 63) host[hi++] = *arg++;
    host[hi] = '\0';

    // Parse optional path (remainder after spaces, default "/")
    while (*arg == ' ') arg++;
    const char* path = "/";
    char path_buf[128];
    if (*arg) {
        int pi = 0;
        while (*arg && pi < 127) path_buf[pi++] = *arg++;
        path_buf[pi] = '\0';
        path = path_buf;
    }

    // Resolve host to IP
    pt::uint32_t ip = parse_ip(host);
    if (ip == 0) {
        vterm_printf("Resolving %s...\n", host);
        if (!dns_resolve(host, 100, ip) || ip == 0) {
            vterm_printf("wget: cannot resolve '%s'\n", host);
            return;
        }
    }

    vterm_printf("Connecting to ");
    print_ip(ip);
    vterm_printf(":80...\n");

    TcpSocket* sock = tcp_connect(ip, 80, 250);
    if (!sock) {
        vterm_printf("wget: connection failed\n");
        return;
    }
    vterm_printf("Connected.\n");

    // Build HTTP/1.0 GET request
    static pt::uint8_t req[512];
    int ri = 0;
    const char* parts[] = { "GET ", path, " HTTP/1.0\r\nHost: ", host,
                             "\r\nConnection: close\r\n\r\n", nullptr };
    for (int p = 0; parts[p]; p++)
        for (int i = 0; parts[p][i] && ri < 511; i++)
            req[ri++] = (pt::uint8_t)parts[p][i];

    tcp_write(sock, req, (pt::uint32_t)ri);

    // Read and print response (up to 8 KB)
    static pt::uint8_t rbuf[512];
    int total = 0;
    while (total < 8192) {
        int n = tcp_read(sock, rbuf, sizeof(rbuf) - 1, 500);
        if (n <= 0) break;
        rbuf[n] = '\0';
        vterm_printf("%s", (const char*)rbuf);
        total += n;
    }
    vterm_printf("\n--- %d bytes received ---\n", total);

    tcp_close(sock);
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
        vterm_printf("Usage: cancel <timer_id>\n");
    } else {
        timer_cancel(timer_id);
    }
}

bool Shell::execute(const char* cmd) {
    // Add to history first
    add_to_history(cmd);

    if (!memcmp(cmd, mem_cmd, sizeof(mem_cmd))) {
        execute_mem(cmd);
    }
    else if (!memcmp(cmd, ticks_cmd, sizeof(ticks_cmd))) {
        execute_ticks(cmd);
    }
    else if (!memcmp(cmd, "alloc", 5)) {
        execute_alloc(cmd);
    }
    else if (!memcmp(cmd, clear_blue_cmd, sizeof(clear_blue_cmd))) {
        execute_clear_color(cmd, 0, 0, 255);
    }
    else if (!memcmp(cmd, clear_green_cmd, sizeof(clear_green_cmd))) {
        execute_clear_color(cmd, 0, 255, 0);
    }
    else if (!memcmp(cmd, clear_red_cmd, sizeof(clear_red_cmd))) {
        execute_clear_color(cmd, 255, 0, 0);
    }
    else if (!memcmp(cmd, map_cmd, 3)) {  // "map" is 3 chars, don't include null terminator
        execute_map(cmd);
    }
    else if (!memcmp(cmd, vmm_cmd, sizeof(vmm_cmd))) {
        execute_vmm(cmd);
    }
    else if (!memcmp(cmd, pci_cmd, sizeof(pci_cmd))) {
        execute_pci(cmd);
    }
    else if (!memcmp(cmd, history_cmd, sizeof(history_cmd))) {
        execute_history(cmd);
    }
    else if (!memcmp(cmd, ls_cmd, sizeof(ls_cmd))) {
        execute_ls(cmd);
    }
    else if (!memcmp(cmd, disk_cmd, sizeof(disk_cmd))) {
        execute_disk(cmd);
    }
    else if (!memcmp(cmd, play_cmd, 5)) {
        execute_play(cmd);
    }
    else if (!memcmp(cmd, cat_cmd, 4)) {
        execute_cat(cmd);
    }
    else if (!memcmp(cmd, help_cmd, sizeof(help_cmd))) {
        execute_help(cmd);
    }
    else if (!memcmp(cmd, echo_cmd, 5)) {
        execute_echo(cmd);
    }
    else if (!memcmp(cmd, clear_cmd, sizeof(clear_cmd))) {
        execute_clear(cmd);
    }
    else if (!memcmp(cmd, timers_cmd, sizeof(timers_cmd))) {
        execute_timers(cmd);
    }
    else if (!memcmp(cmd, cancel_cmd, 7)) {
        execute_cancel(cmd);
    }
    else if (!memcmp(cmd, shutdown_cmd, sizeof(shutdown_cmd))) {
        execute_shutdown(cmd);
    }
    else if (!memcmp(cmd, reboot_cmd, sizeof(reboot_cmd))) {
        execute_reboot(cmd);
    }
    else if (!memcmp(cmd, task_cmd, 4)) {
        execute_task(cmd);
    }
    else if (!memcmp(cmd, write_cmd, 6)) {
        execute_write(cmd);
    }
    else if (!memcmp(cmd, rm_cmd, 3)) {
        execute_rm(cmd);
    }
    else if (!memcmp(cmd, exec_cmd, 5)) {
        execute_exec(cmd);
    }
    else if (!memcmp(cmd, "ping", 4) && (cmd[4] == ' ' || cmd[4] == '\0')) {
        execute_ping(cmd);
    }
    else if (!memcmp(cmd, net_cmd, 3) && (cmd[3] == '\0' || cmd[3] == ' ')) {
        execute_net(cmd);
    }
    else if (!memcmp(cmd, dhcp_cmd, 4) && (cmd[4] == '\0' || cmd[4] == ' ')) {
        execute_dhcp(cmd);
    }
    else if (!memcmp(cmd, nslookup_cmd, 8) && (cmd[8] == '\0' || cmd[8] == ' ')) {
        execute_nslookup(cmd);
    }
    else if (!memcmp(cmd, wget_cmd, 4) && (cmd[4] == ' ' || cmd[4] == '\0')) {
        execute_wget(cmd);
    }
    else if (!memcmp(cmd, quit_cmd, sizeof(quit_cmd)))
    {
        vterm_printf("bye bye ;)\n");
        return false;
    }
    else {
        vterm_printf("Invalid command\n");
    }

    return true;
}

Shell shell;
