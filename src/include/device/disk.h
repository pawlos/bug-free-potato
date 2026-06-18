#pragma once

#include "defs.h"

class Disk {
public:
    static void initialize();
    static bool read_sector(pt::uint32_t lba, void* buffer);
    static bool read_sectors(pt::uint32_t lba, pt::uint8_t count, void* buffer);
    static bool write_sector(pt::uint32_t lba, const void* buffer);
    static pt::uint32_t get_sector_count();
    static bool is_present();

    // Raw sector I/O (bypasses the cache layer) — used by disk_cache
    static bool raw_read_sectors(pt::uint32_t lba, pt::uint8_t count, void* buffer);

private:
    static bool present;
    static pt::uint32_t sector_count;
};
