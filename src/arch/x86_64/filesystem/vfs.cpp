#include "fs/vfs.h"
#include "fs/fat12.h"
#include "fs/fat32.h"
#include "device/disk.h"
#include "kernel.h"

// Placement new: construct an object in pre-allocated storage without relying
// on the C++ runtime .init_array / global constructor mechanism, which this
// kernel does not invoke.
inline void* operator new(__SIZE_TYPE__, void* p) noexcept { return p; }

// Raw storage for filesystem instances — placement-new'd in VFS::mount().
static char fat12_storage[sizeof(FAT12)] __attribute__((aligned(__alignof__(FAT12))));
static char fat32_storage[sizeof(FAT32)] __attribute__((aligned(__alignof__(FAT32))));

Filesystem* VFS::active_fs = nullptr;

// Sector buffer for reading BPB during mount detection
static pt::uint8_t vfs_sector_buf[512] __attribute__((aligned(4)));

bool VFS::mount() {
    if (!Disk::is_present()) {
        klog("[VFS] No disk present\n");
        return false;
    }

    if (!Disk::read_sector(0, vfs_sector_buf)) {
        klog("[VFS] Failed to read boot sector\n");
        return false;
    }

    // Parse BPB fields needed to determine FAT type.
    // The first 36 bytes are identical for FAT12/16/32.
    const FAT12_BPB* bpb = reinterpret_cast<const FAT12_BPB*>(vfs_sector_buf);

    if (bpb->bytes_per_sector == 0 || bpb->sectors_per_cluster == 0) {
        klog("[VFS] Invalid BPB, cannot determine filesystem type\n");
        return false;
    }

    // sectors_per_fat is 0 for FAT32; use the 32-bit field at offset 36 instead.
    pt::uint32_t sectors_per_fat;
    if (bpb->sectors_per_fat != 0) {
        sectors_per_fat = bpb->sectors_per_fat;
    } else {
        // FAT32: sectors_per_fat_32 lives at offset 36 in the boot sector.
        sectors_per_fat = *reinterpret_cast<const pt::uint32_t*>(vfs_sector_buf + 36);
    }

    // root_entry_count == 0 for FAT32, so root_dir_sectors == 0 there too.
    pt::uint32_t root_dir_sectors = ((bpb->root_entry_count * 32) + (bpb->bytes_per_sector - 1))
                                    / bpb->bytes_per_sector;
    pt::uint32_t fat_start        = bpb->reserved_sector_count;
    pt::uint32_t data_start       = fat_start
                                    + (bpb->fat_count * sectors_per_fat)
                                    + root_dir_sectors;
    pt::uint32_t total_sectors    = (bpb->total_sectors_16 != 0)
                                    ? bpb->total_sectors_16
                                    : bpb->total_sectors_32;
    pt::uint32_t data_sectors     = (total_sectors > data_start) ? total_sectors - data_start : 0;
    pt::uint32_t total_clusters   = data_sectors / bpb->sectors_per_cluster;

    if (total_clusters < 4085) {
        klog("[VFS] total_clusters=%d -> FAT12\n", total_clusters);
        active_fs = new (fat12_storage) FAT12();
    } else if (total_clusters < 65525) {
        klog("[VFS] total_clusters=%d -> FAT16 (not yet supported)\n", total_clusters);
        return false;
    } else {
        klog("[VFS] total_clusters=%d -> FAT32\n", total_clusters);
        active_fs = new (fat32_storage) FAT32();
    }

    return active_fs->mount();
}

bool VFS::open_file(const char* filename, File* file) {
    if (!active_fs) return false;
    // Try full path first (supports subdirectories)
    if (active_fs->open_file(filename, file))
        return true;
    // Fall back to basename-only for backward compat (e.g. Quake's
    // "//id1/pak0.pak" → "pak0.pak" searched in root)
    const char* base = filename;
    for (const char* p = filename; *p; p++)
        if (*p == '/') base = p + 1;
    if (base != filename && *base)
        return active_fs->open_file(base, file);
    return false;
}

pt::uint32_t VFS::read_file(File* file, void* buffer, pt::uint32_t bytes_to_read) {
    if (!active_fs) return 0;
    return active_fs->read_file(file, buffer, bytes_to_read);
}

pt::uint32_t VFS::write_file(File* file, const void* buffer, pt::uint32_t bytes_to_write) {
    if (!active_fs) return 0;
    return active_fs->write_file(file, buffer, bytes_to_write);
}

bool VFS::open_file_write(const char* filename, File* out) {
    if (!active_fs) return false;
    return active_fs->open_file_write(filename, out);
}

pt::uint32_t VFS::seek_file(File* file, pt::int32_t offset, int whence) {
    if (!active_fs) return (pt::uint32_t)-1;
    return active_fs->seek_file(file, offset, whence);
}

void VFS::close_file(File* file) {
    if (!active_fs) return;
    active_fs->close_file(file);
}

bool VFS::file_exists(const char* filename) {
    if (!active_fs) return false;
    if (active_fs->file_exists(filename))
        return true;
    const char* base = filename;
    for (const char* p = filename; *p; p++)
        if (*p == '/') base = p + 1;
    if (base != filename && *base)
        return active_fs->file_exists(base);
    return false;
}

void VFS::list_root_directory() {
    if (!active_fs) return;
    active_fs->list_root_directory();
}

bool VFS::create_file(const char* filename, const pt::uint8_t* data, pt::uint32_t size) {
    if (!active_fs) return false;
    return active_fs->create_file(filename, data, size);
}

bool VFS::delete_file(const char* filename) {
    if (!active_fs) return false;
    return active_fs->delete_file(filename);
}

bool VFS::readdir(int idx, char* name_out, pt::uint32_t* size_out) {
    if (!active_fs) return false;
    return active_fs->readdir(idx, name_out, size_out);
}

int VFS::readdir_ex(const char* path, int idx, char* name_out,
                    pt::uint32_t* size_out, pt::uint8_t* type_out) {
    if (!active_fs) return 0;
    return active_fs->readdir_ex(path, idx, name_out, size_out, type_out);
}

bool VFS::stat_file(const char* filename, StatResult* out) {
    if (!active_fs) return false;
    if (active_fs->stat_file(filename, out))
        return true;
    const char* base = filename;
    for (const char* p = filename; *p; p++)
        if (*p == '/') base = p + 1;
    if (base != filename && *base)
        return active_fs->stat_file(base, out);
    return false;
}

void VFS::list_directory(const char* path) {
    if (!active_fs) return;
    active_fs->list_directory(path);
}

pt::uint32_t VFS::get_bytes_per_cluster() {
    if (!active_fs) return 0;
    return active_fs->get_bytes_per_cluster();
}

pt::uint32_t VFS::get_free_space() {
    if (!active_fs) return 0;
    return active_fs->get_free_space();
}

pt::uint32_t VFS::get_total_space() {
    if (!active_fs) return 0;
    return active_fs->get_total_space();
}
