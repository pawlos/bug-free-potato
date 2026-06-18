#include "device/disk.h"
#include "device/disk_cache.h"
#include "device/ide.h"
#include "device/ahci.h"
#include "kernel.h"

bool Disk::present = false;
pt::uint32_t Disk::sector_count = 0;

// Whether we're using AHCI or IDE backend
static bool use_ahci = false;

void Disk::initialize() {
    klog("[DISK] Initializing disk subsystem...\n");

    // Try AHCI first (DMA-based, much faster)
    if (AHCI::initialize()) {
        use_ahci = true;
        present = true;
        sector_count = AHCI::get_sector_count(0);
        klog("[DISK] Using AHCI: %d sectors across %d drive(s)\n",
             sector_count, AHCI::get_drive_count());
        return;
    }

    // Fall back to PIO IDE
    klog("[DISK] AHCI not available, trying IDE...\n");
    IDE::initialize();

    if (IDE::is_drive_present(0)) {
        present = true;
        sector_count = IDE::get_sector_count(0);
        klog("[DISK] Using IDE: master drive, %d sectors\n", sector_count);
    } else {
        klog("[DISK] No drives found via AHCI or IDE\n");
    }
}

bool Disk::read_sector(pt::uint32_t lba, void* buffer) {
    if (!present) return false;
    return disk_cache_read(lba, buffer);
}

bool Disk::read_sectors(pt::uint32_t lba, pt::uint8_t count, void* buffer) {
    if (!present) return false;
    return disk_cache_read_multi(lba, count, buffer);
}

bool Disk::write_sector(pt::uint32_t lba, const void* buffer) {
    if (!present) return false;
    disk_cache_invalidate();
    if (use_ahci)
        return AHCI::write_sectors(0, lba, 1, buffer);
    return IDE::write_sectors(0, lba, 1, buffer);
}

bool Disk::raw_read_sectors(pt::uint32_t lba, pt::uint8_t count, void* buffer) {
    if (!present) return false;
    if (use_ahci)
        return AHCI::read_sectors(0, lba, count, buffer);
    return IDE::read_sectors(0, lba, count, buffer);
}

pt::uint32_t Disk::get_sector_count() {
    return sector_count;
}

bool Disk::is_present() {
    return present;
}
