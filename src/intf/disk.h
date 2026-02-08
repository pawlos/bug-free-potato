#pragma once

#include "defs.h"

// Disk abstraction layer
// Currently supports IDE drives with FAT12 filesystem

class Disk {
public:
    static void initialize();
    static bool read_sector(pt::uint32_t lba, void* buffer);
    static bool read_sectors(pt::uint32_t lba, pt::uint8_t count, void* buffer);
    static pt::uint32_t get_sector_count();
    static bool is_present();

private:
    static bool present;
    static pt::uint32_t sector_count;
};
