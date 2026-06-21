#pragma once
#include "defs.h"

// LRU sector cache with read-ahead for the Disk layer.
// Sits between Disk::read_sector*() and IDE::read_sectors().
// Allocates a fixed pool from the kernel heap at init time.

constexpr pt::size_t CACHE_LINES       = 64;
// Read-ahead window per cache miss. The disk path is per-command-overhead
// bound, not transfer bound: `diskbench` measured raw sequential reads at
// 73 MB/s @ 8 sectors → 155 @ 16 → 221 @ 32 → 173 @ 64 — throughput climbs as
// the command count drops, then regresses past 32. So 32 sectors (16 KB) is
// the empirical peak, and also the largest read-ahead that still fits the AHCI
// 8-PRD cap (AHCI_PRDT_MAX) for an unaligned cache line (worst case 5 PRDs);
// 64 sectors needs 9 PRDs and would be rejected. Pool = CACHE_LINES *
// READAHEAD_SECTORS * 512 = 1 MB.
constexpr pt::size_t READAHEAD_SECTORS = 32;
constexpr pt::size_t SECTOR_SIZE       = 512;

void disk_cache_init();
bool disk_cache_read(pt::uint32_t lba, void* buffer);
bool disk_cache_read_multi(pt::uint32_t lba, pt::uint8_t count, void* buffer);
void disk_cache_invalidate();
