#pragma once
#include "defs.h"

// LRU sector cache with read-ahead for the Disk layer.
// Sits between Disk::read_sector*() and IDE::read_sectors().
// Allocates a fixed pool from the kernel heap at init time.

constexpr pt::size_t CACHE_LINES       = 64;
constexpr pt::size_t READAHEAD_SECTORS = 8;
constexpr pt::size_t SECTOR_SIZE       = 512;

void disk_cache_init();
bool disk_cache_read(pt::uint32_t lba, void* buffer);
bool disk_cache_read_multi(pt::uint32_t lba, pt::uint8_t count, void* buffer);
void disk_cache_invalidate();
