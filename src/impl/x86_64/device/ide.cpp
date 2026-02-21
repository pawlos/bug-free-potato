#include "ide.h"
#include "io.h"
#include "kernel.h"

IDEDevice IDE::drives[2];
bool IDE::initialized = false;

// Simple delay
static void ide_delay() {
    for (volatile int i = 0; i < 1000; i++);
}

// Long delay for reset
static void ide_reset_delay() {
    for (volatile int i = 0; i < 100000; i++);
}

// Poll until not busy and optionally wait for DRDY
static bool ide_wait_ready(bool check_drdy) {
    for (int timeout = 0; timeout < 1000000; timeout++) {
        pt::uint8_t status = IO::inb(IDE_PRIMARY_STATUS);
        
        // Check for error
        if (status & IDE_STATUS_ERR) {
            return false;
        }
        
        // Wait for BSY to clear
        if (status & IDE_STATUS_BSY) {
            ide_delay();
            continue;
        }
        
        // If we need DRDY, check for it
        if (check_drdy && !(status & IDE_STATUS_RDY)) {
            ide_delay();
            continue;
        }
        
        return true;
    }
    return false;
}

// Wait for DRQ (data ready) or error
static bool ide_wait_drq() {
    for (int timeout = 0; timeout < 1000000; timeout++) {
        pt::uint8_t status = IO::inb(IDE_PRIMARY_STATUS);
        
        if (status & IDE_STATUS_ERR) {
            return false;
        }
        
        if (status & IDE_STATUS_DRQ) {
            return true;
        }
        
        ide_delay();
    }
    return false;
}

// Software reset of IDE controller
static void ide_software_reset() {
    IO::outb(IDE_PRIMARY_CONTROL, 0x04);
    ide_delay();
    IO::outb(IDE_PRIMARY_CONTROL, 0x00);
    ide_reset_delay();
}

// Get sector count from IDENTIFY DEVICE command
// Returns sector count from words 60-61 (LBA28), or 0 on failure
static pt::uint32_t ide_get_sector_count() {
    // Issue IDENTIFY DEVICE command (0xEC)
    IO::outb(IDE_PRIMARY_COMMAND, 0xEC);

    // Wait for data ready
    if (!ide_wait_drq()) {
        klog("[IDE] IDENTIFY DEVICE: no DRQ\n");
        return 0;
    }

    // Read 256 words (512 bytes) from the data port
    pt::uint16_t identify[256];
    for (int i = 0; i < 256; i++) {
        identify[i] = IO::inw(IDE_PRIMARY_DATA);
    }

    // Words 60-61 contain LBA28 sector count (little-endian)
    // Word 60 = low 16 bits, Word 61 = high 16 bits
    pt::uint32_t sector_count = identify[60] | (identify[61] << 16);

    klog("[IDE] Sector count from IDENTIFY: %d\n", sector_count);
    return sector_count;
}

void IDE::initialize() {
    if (initialized) return;
    
    klog("[IDE] Initializing...\n");
    
    drives[0].present = false;
    drives[1].present = false;
    
    // Perform software reset
    ide_software_reset();
    
    // Select master drive with LBA mode enabled (bit 6)
    // 0xE0 = 1110 0000 = LBA mode (bit 6) + master (bit 4)
    IO::outb(IDE_PRIMARY_DRIVE_SELECT, 0xE0);
    ide_delay();
    
    // Wait for drive to be ready
    if (!ide_wait_ready(true)) {
        klog("[IDE] Drive not responding after reset\n");
    }
    
    pt::uint8_t status = IO::inb(IDE_PRIMARY_STATUS);
    klog("[IDE] Status after init: %x\n", status);
    
    // Check if drive exists (status should not be 0xFF or 0x00)
    if (status != 0xFF && status != 0x00) {
        drives[0].present = true;
        drives[0].is_master = true;

        // Get real sector count from IDENTIFY DEVICE
        pt::uint32_t sector_count = ide_get_sector_count();
        if (sector_count == 0) {
            // Fallback if IDENTIFY fails
            klog("[IDE] IDENTIFY failed, using default sector count\n");
            sector_count = 20480;
        }
        drives[0].sector_count = sector_count;

        const char* model = "QEMU HARDDISK";
        for (int i = 0; i < 40 && model[i]; i++) {
            drives[0].model[i] = model[i];
        }
        drives[0].model[40] = '\0';

        klog("[IDE] Master drive detected (%d sectors)\n", sector_count);
    } else {
        klog("[IDE] No master drive detected\n");
    }
    
    initialized = true;
    klog("[IDE] Init done\n");
}

bool IDE::read_sectors(pt::uint8_t drive, pt::uint32_t lba, pt::uint8_t sector_count, void* buffer) {
    if (drive >= 2 || !drives[drive].present) {
        return false;
    }
    
    if (sector_count == 0 || buffer == nullptr) {
        return false;
    }
    
    pt::uint8_t* byte_buffer = static_cast<pt::uint8_t*>(buffer);
    
    for (pt::uint8_t s = 0; s < sector_count; s++) {
        pt::uint32_t current_lba = lba + s;
        
        // Disable interrupts during operation
        disable_interrupts();
        
        // Wait for drive to be ready
        if (!ide_wait_ready(true)) {
            klog("[IDE] Drive not ready before command\n");
            enable_interrupts();
            return false;
        }
        
        // Select drive with LBA mode enabled
        // 0xE0 = LBA mode (bit 6) + master (bit 4) + LBA bits 24-27
        pt::uint8_t drive_select = 0xE0 | ((current_lba >> 24) & 0x0F);
        IO::outb(IDE_PRIMARY_DRIVE_SELECT, drive_select);
        ide_delay();
        
        // Wait again after drive select
        if (!ide_wait_ready(false)) {
            klog("[IDE] Drive busy after select\n");
            enable_interrupts();
            return false;
        }
        
        // Set sector count
        IO::outb(IDE_PRIMARY_SECTOR_COUNT, 1);
        ide_delay();
        
        // Set LBA address (LBA low, mid, high)
        IO::outb(IDE_PRIMARY_LBA_LOW, current_lba & 0xFF);
        IO::outb(IDE_PRIMARY_LBA_MID, (current_lba >> 8) & 0xFF);
        IO::outb(IDE_PRIMARY_LBA_HIGH, (current_lba >> 16) & 0xFF);
        ide_delay();
        
        // Send READ SECTORS command
        IO::outb(IDE_PRIMARY_COMMAND, IDE_CMD_READ_SECTORS);
        
        // Wait for data ready
        if (!ide_wait_drq()) {
            pt::uint8_t status = IO::inb(IDE_PRIMARY_STATUS);
            pt::uint8_t error = IO::inb(IDE_PRIMARY_ERROR);
            klog("[IDE] DRQ timeout: status=%x error=%x\n", status, error);
            enable_interrupts();
            return false;
        }
        
        // Read 256 words (512 bytes)
        pt::uint16_t* word_ptr = reinterpret_cast<pt::uint16_t*>(byte_buffer + s * 512);
        for (int i = 0; i < 256; i++) {
            word_ptr[i] = IO::inw(IDE_PRIMARY_DATA);
        }
        
        // Re-enable interrupts
        enable_interrupts();
    }
    
    return true;
}

bool IDE::write_sectors(pt::uint8_t drive, pt::uint32_t lba, pt::uint8_t sector_count, const void* buffer) {
    if (drive >= 2 || !drives[drive].present) {
        return false;
    }

    if (sector_count == 0 || buffer == nullptr) {
        return false;
    }

    const pt::uint8_t* byte_buffer = static_cast<const pt::uint8_t*>(buffer);

    for (pt::uint8_t s = 0; s < sector_count; s++) {
        pt::uint32_t current_lba = lba + s;

        disable_interrupts();

        if (!ide_wait_ready(true)) {
            klog("[IDE] Drive not ready before write command\n");
            enable_interrupts();
            return false;
        }

        pt::uint8_t drive_select = 0xE0 | ((current_lba >> 24) & 0x0F);
        IO::outb(IDE_PRIMARY_DRIVE_SELECT, drive_select);
        ide_delay();

        if (!ide_wait_ready(false)) {
            klog("[IDE] Drive busy after select (write)\n");
            enable_interrupts();
            return false;
        }

        IO::outb(IDE_PRIMARY_SECTOR_COUNT, 1);
        ide_delay();

        IO::outb(IDE_PRIMARY_LBA_LOW,  current_lba & 0xFF);
        IO::outb(IDE_PRIMARY_LBA_MID,  (current_lba >> 8) & 0xFF);
        IO::outb(IDE_PRIMARY_LBA_HIGH, (current_lba >> 16) & 0xFF);
        ide_delay();

        IO::outb(IDE_PRIMARY_COMMAND, IDE_CMD_WRITE_SECTORS);

        if (!ide_wait_drq()) {
            pt::uint8_t status = IO::inb(IDE_PRIMARY_STATUS);
            pt::uint8_t error  = IO::inb(IDE_PRIMARY_ERROR);
            klog("[IDE] Write DRQ timeout: status=%x error=%x\n", status, error);
            enable_interrupts();
            return false;
        }

        // Write 256 words (512 bytes)
        const pt::uint16_t* word_ptr = reinterpret_cast<const pt::uint16_t*>(byte_buffer + s * 512);
        for (int i = 0; i < 256; i++) {
            IO::outw(IDE_PRIMARY_DATA, word_ptr[i]);
        }

        // Cache flush
        IO::outb(IDE_PRIMARY_COMMAND, 0xE7);
        if (!ide_wait_ready(false)) {
            klog("[IDE] Cache flush wait failed\n");
            enable_interrupts();
            return false;
        }

        enable_interrupts();
    }

    return true;
}

bool IDE::is_drive_present(pt::uint8_t drive) {
    if (!initialized) initialize();
    return (drive < 2) ? drives[drive].present : false;
}

pt::uint32_t IDE::get_sector_count(pt::uint8_t drive) {
    if (!initialized) initialize();
    return (drive < 2 && drives[drive].present) ? drives[drive].sector_count : 0;
}
