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
#include "task.h"
#include "disk.h"
#include "fat12.h"
#include "ide.h"
#include "ac97.h"

// These will be loaded from FAT12 disk
const char* Logo = nullptr;
const unsigned char* PotatoLogo = nullptr;
pt::uint8_t* BootSound = nullptr;
pt::uint32_t BootSoundSize = 0;

extern char get_char();
static IDT idt;
VMM vmm;

pt::uint64_t old_ticks = 0;

void five_minute_callback(void* data) {
    (void)data;
    klog("[TIMER] 5 minutes has elapsed\n");
}

void thirty_minute_callback(void* data) {
    (void)data;
    klog("[TIMER] 30 minutes has elapsed\n");
}

void blink_task_fn() {
    Framebuffer* fb = Framebuffer::get_instance();
    constexpr pt::uint32_t DOT_W = 12;
    constexpr pt::uint32_t DOT_H = 12;
    const pt::uint32_t DOT_X = fb->get_width()  - DOT_W;  // flush to right edge
    const pt::uint32_t DOT_Y = 0;                          // flush to top edge
    bool dot_on = false;

    while (true) {
        bool should_be_on = (get_ticks() / 25) & 1;  // toggle ~every 0.5s at 50Hz
        if (should_be_on != dot_on) {
            dot_on = should_be_on;
            if (dot_on)
                fb->FillRect(DOT_X, DOT_Y, DOT_W, DOT_H, 0, 255, 0);
            else
                fb->FillRect(DOT_X, DOT_Y, DOT_W, DOT_H, 0, 0, 0);
        }
        TaskScheduler::task_yield();
    }
}

ASMCALL void kernel_main(boot_info* boot_info, void* l4_page_table) {
    klog("[MAIN] Welcome to 64-bit potat OS\n");

    auto bi = BootInfo(boot_info);

    const auto boot_fb = bi.get_framebuffer();

    init_mouse(boot_fb->framebuffer_width, boot_fb->framebuffer_height);
    TaskScheduler::initialize();
    init_timer(50);

    idt.initialize();

    vmm = VMM(bi.get_memory_maps(), l4_page_table);

    // Initialize disk subsystem (optional - may fail)
    Disk::initialize();

    // Try to mount FAT12 filesystem
    bool fat12_mounted = FAT12::initialize();

    if (fat12_mounted) {
        // Load Logo from potato.txt
        FAT12_File logo_file;
        if (FAT12::open_file("potato.txt", &logo_file)) {
            char* logo_buffer = (char*)vmm.kmalloc(logo_file.file_size + 1);
            if (logo_buffer) {
                pt::uint32_t bytes_read = FAT12::read_file(&logo_file, logo_buffer, logo_file.file_size);
                logo_buffer[bytes_read] = '\0';
                Logo = logo_buffer;
                klog("[MAIN] Loaded Logo from potato.txt (%d bytes)\n", bytes_read);
            }
            FAT12::close_file(&logo_file);
        } else {
            klog("[MAIN] Warning: potato.txt not found on disk\n");
        }

        // Load PotatoLogo from potato.raw
        FAT12_File raw_file;
        if (FAT12::open_file("potato.raw", &raw_file)) {
            unsigned char* raw_buffer = (unsigned char*)vmm.kmalloc(raw_file.file_size);
            if (raw_buffer) {
                pt::uint32_t bytes_read = FAT12::read_file(&raw_file, raw_buffer, raw_file.file_size);
                PotatoLogo = raw_buffer;
                klog("[MAIN] Loaded PotatoLogo from potato.raw (%d bytes)\n", bytes_read);
            }
            FAT12::close_file(&raw_file);
        } else {
            klog("[MAIN] Warning: potato.raw not found on disk\n");
        }

        // Load boot sound from boot.raw (16-bit signed LE stereo PCM, 48 kHz)
        FAT12_File sound_file;
        if (FAT12::open_file("boot.raw", &sound_file)) {
            BootSound = (pt::uint8_t*)vmm.kmalloc(sound_file.file_size);
            if (BootSound) {
                BootSoundSize = FAT12::read_file(&sound_file, BootSound, sound_file.file_size);
                klog("[MAIN] Loaded boot sound from boot.raw (%d bytes)\n", BootSoundSize);
            }
            FAT12::close_file(&sound_file);
        } else {
            klog("[MAIN] Warning: boot.raw not found on disk\n");
        }
    }

    // Initialize AC97 audio controller
    if (AC97::initialize()) {
        if (BootSound && BootSoundSize > 0) {
            // Play the boot sound loaded from disk
            klog("[MAIN] Playing boot sound (%d bytes)...\n", BootSoundSize);
            AC97::play_pcm(BootSound, BootSoundSize, 48000);
        } else {
            // No boot sound file - play a short beep to confirm audio is working
            klog("[MAIN] Playing startup beep...\n");
            AC97::play_beep(880, 300);
        }
    }

    constexpr pt::uint64_t five_minutes_in_ticks = 5 * 60 * 50;
    constexpr pt::uint64_t thirty_minutes_in_ticks = 30 * 60 * 50;
    timer_create(five_minutes_in_ticks, true, five_minute_callback, nullptr);
    timer_create(thirty_minutes_in_ticks, true, thirty_minute_callback, nullptr);

    Framebuffer::Init(boot_fb);
    TaskScheduler::create_task(&blink_task_fn);

    TerminalPrinter terminal;

    terminal.print_clear();
    terminal.print_set_color(PRINT_COLOR_YELLOW, PRINT_COLOR_BLACK);

    // Print logo if loaded
    if (Logo) {
        terminal.print_str(Logo);
    }

    const pt::uint32_t img_width = 197;
    const pt::uint32_t img_height = 197;
    const pt::uint32_t center_x = (boot_fb->framebuffer_width - img_width) / 2;
    const pt::uint32_t center_y = (boot_fb->framebuffer_height - img_height) / 2;
    
    // Draw potato logo if loaded
    if (PotatoLogo) {
        Framebuffer::get_instance()->Draw(PotatoLogo, center_x, center_y, img_width, img_height);
    }

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
    }
    Framebuffer::get_instance()->Free();
    halt();
}