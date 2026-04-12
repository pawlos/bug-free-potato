#include "device/disk_cache.h"
#include "device/ide.h"
#include "device/disk.h"
#include "kernel.h"
#include "virtual.h"

struct CacheLine {
    pt::uint32_t lba;       // starting LBA of this run (0 = unused)
    pt::uint8_t  count;     // sectors in this line (1–READAHEAD_SECTORS)
    pt::uint8_t  age;       // LRU age (0 = most recent, higher = older)
    pt::uint8_t* data;      // points into the data pool
};

static CacheLine lines[CACHE_LINES];
static pt::uint8_t* pool = nullptr;

void disk_cache_init()
{
    pt::size_t pool_size = CACHE_LINES * READAHEAD_SECTORS * SECTOR_SIZE;
    pool = static_cast<pt::uint8_t*>(vmm.kcalloc(pool_size));
    if (!pool) {
        klog("[DISK_CACHE] Failed to allocate %d bytes\n", pool_size);
        return;
    }
    for (pt::size_t i = 0; i < CACHE_LINES; i++) {
        lines[i].lba   = 0;
        lines[i].count = 0;
        lines[i].age   = 255;
        lines[i].data  = pool + i * READAHEAD_SECTORS * SECTOR_SIZE;
    }
    klog("[DISK_CACHE] Initialized: %d lines, %d-sector read-ahead, %d KB pool\n",
         CACHE_LINES, READAHEAD_SECTORS, pool_size / 1024);
}

// Find a cache line containing the given LBA. Returns index or -1.
static int cache_find(pt::uint32_t lba)
{
    for (pt::size_t i = 0; i < CACHE_LINES; i++) {
        if (lines[i].count > 0 &&
            lba >= lines[i].lba &&
            lba < lines[i].lba + lines[i].count)
            return (int)i;
    }
    return -1;
}

// Age all lines except the accessed one.
static void cache_touch(pt::size_t hit_idx)
{
    lines[hit_idx].age = 0;
    for (pt::size_t i = 0; i < CACHE_LINES; i++) {
        if (i != hit_idx && lines[i].count > 0 && lines[i].age < 255)
            lines[i].age++;
    }
}

// Find the oldest line for eviction.
static pt::size_t cache_evict_idx()
{
    pt::size_t best = 0;
    pt::uint8_t best_age = 0;
    for (pt::size_t i = 0; i < CACHE_LINES; i++) {
        if (lines[i].count == 0) return i;  // empty slot — use immediately
        if (lines[i].age >= best_age) {
            best_age = lines[i].age;
            best = i;
        }
    }
    return best;
}

bool disk_cache_read(pt::uint32_t lba, void* buffer)
{
    if (!pool) {
        // Cache not initialized — fall through to IDE directly.
        return IDE::read_sectors(0, lba, 1, buffer);
    }

    // Check cache.
    int hit = cache_find(lba);
    if (hit >= 0) {
        pt::uint32_t offset = (lba - lines[hit].lba) * SECTOR_SIZE;
        memcpy(buffer, lines[hit].data + offset, SECTOR_SIZE);
        cache_touch((pt::size_t)hit);
        return true;
    }

    // Miss — read ahead.
    pt::size_t idx = cache_evict_idx();
    pt::uint32_t disk_sectors = Disk::get_sector_count();
    pt::uint8_t ra = READAHEAD_SECTORS;
    if (disk_sectors > 0 && lba + ra > disk_sectors)
        ra = (pt::uint8_t)(disk_sectors - lba);
    if (ra == 0) ra = 1;

    if (!IDE::read_sectors(0, lba, ra, lines[idx].data))
        return false;

    lines[idx].lba   = lba;
    lines[idx].count = ra;
    cache_touch(idx);

    memcpy(buffer, lines[idx].data, SECTOR_SIZE);
    return true;
}

bool disk_cache_read_multi(pt::uint32_t lba, pt::uint8_t count, void* buffer)
{
    pt::uint8_t* dst = static_cast<pt::uint8_t*>(buffer);
    for (pt::uint8_t i = 0; i < count; i++) {
        if (!disk_cache_read(lba + i, dst + i * SECTOR_SIZE))
            return false;
    }
    return true;
}

void disk_cache_invalidate()
{
    for (pt::size_t i = 0; i < CACHE_LINES; i++) {
        lines[i].count = 0;
        lines[i].age   = 255;
    }
}
