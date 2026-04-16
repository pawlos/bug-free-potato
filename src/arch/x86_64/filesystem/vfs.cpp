#include "fs/vfs.h"
#include "fs/fat12.h"
#include "fs/fat32.h"
#include "fs/procfs.h"
#include "device/disk.h"
#include "device/disk_cache.h"
#include "kernel.h"

// Placement new: construct an object in pre-allocated storage without relying
// on the C++ runtime .init_array / global constructor mechanism, which this
// kernel does not invoke.
inline void* operator new(__SIZE_TYPE__, void* p) noexcept { return p; }

// Raw storage for filesystem instances — placement-new'd in VFS::mount().
static char fat12_storage[sizeof(FAT12)] __attribute__((aligned(__alignof__(FAT12))));
static char fat32_storage[sizeof(FAT32)] __attribute__((aligned(__alignof__(FAT32))));
static char procfs_storage[sizeof(ProcFS)] __attribute__((aligned(__alignof__(ProcFS))));

Filesystem* VFS::active_fs = nullptr;
ProcFS*     VFS::proc_fs   = nullptr;

// ── Path helpers ─────────────────────────────────────────────────────────────

// Returns true if path (after stripping a leading '/') starts with "proc/" or
// equals "proc".  Sets *rest to the part after "proc/" (empty for root).
static bool is_proc_path(const char* path, const char** rest) {
    if (path && path[0] == '/') path++;
    auto ci = [](char a, char b) {
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        return a == b;
    };
    if (!ci(path[0],'p') || !ci(path[1],'r') || !ci(path[2],'o') || !ci(path[3],'c'))
        return false;
    if (path[4] == '\0') { *rest = path + 4; return true; }
    if (path[4] != '/')  return false;
    *rest = path + 5;
    return true;
}

// Strip leading '/' from a path before forwarding to FAT32.
static const char* strip_slash(const char* path) {
    if (path && path[0] == '/') return path + 1;
    return path;
}

// Sector buffer for reading BPB during mount detection
static pt::uint8_t vfs_sector_buf[512] __attribute__((aligned(4)));

bool VFS::mount() {
    if (!Disk::is_present()) {
        klog("[VFS] No disk present\n");
        return false;
    }

    disk_cache_init();

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

    if (!active_fs->mount()) return false;

    // Mount synthetic /proc filesystem.
    proc_fs = new (procfs_storage) ProcFS();
    proc_fs->mount();
    return true;
}

bool VFS::open_file(const char* filename, File* file) {
    const char* rest;
    if (proc_fs && is_proc_path(filename, &rest))
        return proc_fs->open_file(rest, file);
    if (!active_fs) return false;
    filename = strip_slash(filename);
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
    if (file->type == FdType::PROC_FILE)
        return proc_fs ? proc_fs->read_file(file, buffer, bytes_to_read) : 0;
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

bool VFS::open_file_readwrite(const char* filename, File* out) {
    if (!active_fs) return false;
    return active_fs->open_file_readwrite(filename, out);
}

bool VFS::create_directory(const char* path) {
    if (!active_fs) return false;
    return active_fs->create_directory(path);
}

pt::uint32_t VFS::seek_file(File* file, pt::int32_t offset, int whence) {
    if (file->type == FdType::PROC_FILE)
        return proc_fs ? proc_fs->seek_file(file, offset, whence) : (pt::uint32_t)-1;
    if (!active_fs) return (pt::uint32_t)-1;
    return active_fs->seek_file(file, offset, whence);
}

void VFS::close_file(File* file) {
    if (file->type == FdType::PROC_FILE) {
        if (proc_fs) proc_fs->close_file(file);
        return;
    }
    if (!active_fs) return;
    active_fs->close_file(file);
}

bool VFS::file_exists(const char* filename) {
    const char* rest;
    if (proc_fs && is_proc_path(filename, &rest))
        return proc_fs->file_exists(rest);
    if (!active_fs) return false;
    filename = strip_slash(filename);
    if (active_fs->file_exists(filename)) return true;
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
    const char* rest;
    if (proc_fs && is_proc_path(path, &rest))
        return proc_fs->readdir_ex(rest, idx, name_out, size_out, type_out);
    if (!active_fs) return 0;
    return active_fs->readdir_ex(strip_slash(path), idx, name_out, size_out, type_out);
}

bool VFS::stat_file(const char* filename, StatResult* out) {
    const char* rest;
    if (proc_fs && is_proc_path(filename, &rest))
        return false;  // proc files have no FAT timestamps
    if (!active_fs) return false;
    filename = strip_slash(filename);
    if (active_fs->stat_file(filename, out)) return true;
    const char* base = filename;
    for (const char* p = filename; *p; p++)
        if (*p == '/') base = p + 1;
    if (base != filename && *base)
        return active_fs->stat_file(base, out);
    return false;
}

void VFS::list_directory(const char* path) {
    const char* rest;
    if (proc_fs && is_proc_path(path, &rest)) {
        proc_fs->list_directory(rest);
        return;
    }
    if (!active_fs) return;
    active_fs->list_directory(strip_slash(path));
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
