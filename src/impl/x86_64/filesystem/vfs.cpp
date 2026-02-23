#include "vfs.h"
#include "fat12.h"
#include "disk.h"
#include "kernel.h"

// Placement new: construct an object in pre-allocated storage without relying
// on the C++ runtime .init_array / global constructor mechanism, which this
// kernel does not invoke.
inline void* operator new(__SIZE_TYPE__, void* p) noexcept { return p; }

// Raw storage for the FAT12 instance.  Using a char buffer (not a typed
// global) means no global constructor runs — the object is placement-new'd
// inside VFS::mount() the first time it is needed.
static char fat12_storage[sizeof(FAT12)] __attribute__((aligned(__alignof__(FAT12))));

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

    // Parse BPB fields needed to determine FAT type
    const FAT12_BPB* bpb = reinterpret_cast<const FAT12_BPB*>(vfs_sector_buf);

    if (bpb->bytes_per_sector == 0 || bpb->sectors_per_cluster == 0) {
        klog("[VFS] Invalid BPB, cannot determine filesystem type\n");
        return false;
    }

    pt::uint32_t root_dir_sectors = ((bpb->root_entry_count * 32) + (bpb->bytes_per_sector - 1))
                                    / bpb->bytes_per_sector;
    pt::uint32_t fat_start        = bpb->reserved_sector_count;
    pt::uint32_t data_start       = fat_start
                                    + (bpb->fat_count * bpb->sectors_per_fat)
                                    + root_dir_sectors;
    pt::uint32_t total_sectors    = (bpb->total_sectors_16 != 0)
                                    ? bpb->total_sectors_16
                                    : bpb->total_sectors_32;
    pt::uint32_t data_sectors     = (total_sectors > data_start) ? total_sectors - data_start : 0;
    pt::uint32_t total_clusters   = data_sectors / bpb->sectors_per_cluster;

    if (total_clusters < 4085) {
        klog("[VFS] total_clusters=%d -> FAT12\n", total_clusters);
        // Placement-construct FAT12 into the pre-allocated buffer.
        // This sets the vtable pointer and zero-initialises all fields
        // (matching their in-class initialisers) without needing a global ctor.
        active_fs = new (fat12_storage) FAT12();
    } else if (total_clusters < 65525) {
        klog("[VFS] total_clusters=%d -> FAT16 (not yet supported)\n", total_clusters);
        return false;
    } else {
        klog("[VFS] total_clusters=%d -> FAT32 (not yet supported)\n", total_clusters);
        return false;
    }

    return active_fs->mount();
}

bool VFS::open_file(const char* filename, File* file) {
    if (!active_fs) return false;
    return active_fs->open_file(filename, file);
}

pt::uint32_t VFS::read_file(File* file, void* buffer, pt::uint32_t bytes_to_read) {
    if (!active_fs) return 0;
    return active_fs->read_file(file, buffer, bytes_to_read);
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
    return active_fs->file_exists(filename);
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
