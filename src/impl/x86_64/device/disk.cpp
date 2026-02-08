#include "disk.h"
#include "ide.h"
#include "kernel.h"

bool Disk::present = false;
pt::uint32_t Disk::sector_count = 0;

void Disk::initialize() {
    klog("[DISK] Initializing disk subsystem...\n");
    
    // Initialize IDE controller
    IDE::initialize();
    klog("[DISK] IDE initialized\n");
    
    // Check for drives without actually reading
    if (IDE::is_drive_present(0)) {
        present = true;
        sector_count = IDE::get_sector_count(0);
        klog("[DISK] Master drive present: %d sectors\n", sector_count);
    } else {
        klog("[DISK] No drives found\n");
    }
}

bool Disk::read_sector(pt::uint32_t lba, void* buffer) {
    if (!present) {
        return false;
    }
    
    if (IDE::is_drive_present(0)) {
        return IDE::read_sectors(0, lba, 1, buffer);
    }
    
    return false;
}

bool Disk::read_sectors(pt::uint32_t lba, pt::uint8_t count, void* buffer) {
    if (!present) {
        return false;
    }
    
    if (IDE::is_drive_present(0)) {
        return IDE::read_sectors(0, lba, count, buffer);
    }
    
    return false;
}

pt::uint32_t Disk::get_sector_count() {
    return sector_count;
}

bool Disk::is_present() {
    return present;
}
