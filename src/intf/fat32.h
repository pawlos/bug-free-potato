#pragma once

#include "defs.h"
#include "vfs.h"

// FAT32 Extended BPB (starts at offset 36 in boot sector)
struct __attribute__((packed)) FAT32_BPB_EXT {
    pt::uint32_t sectors_per_fat_32;
    pt::uint16_t ext_flags;
    pt::uint16_t fs_version;
    pt::uint32_t root_cluster;
    pt::uint16_t fs_info;
    pt::uint16_t backup_boot_sector;
    pt::uint8_t  reserved[12];
    pt::uint8_t  drive_number;
    pt::uint8_t  reserved1;
    pt::uint8_t  boot_signature;
    pt::uint32_t volume_serial;
    pt::uint8_t  volume_label[11];
    pt::uint8_t  filesystem_type[8];   // "FAT32   "
};

// Full FAT32 boot sector: standard 36-byte BPB + FAT32 extension
struct __attribute__((packed)) FAT32_BPB {
    pt::uint8_t  boot_jump[3];
    pt::uint8_t  oem_name[8];
    pt::uint16_t bytes_per_sector;
    pt::uint8_t  sectors_per_cluster;
    pt::uint16_t reserved_sector_count;
    pt::uint8_t  fat_count;
    pt::uint16_t root_entry_count;     // always 0 for FAT32
    pt::uint16_t total_sectors_16;     // always 0 for FAT32
    pt::uint8_t  media_type;
    pt::uint16_t sectors_per_fat_16;   // always 0 for FAT32
    pt::uint16_t sectors_per_track;
    pt::uint16_t head_count;
    pt::uint32_t hidden_sector_count;
    pt::uint32_t total_sectors_32;
    FAT32_BPB_EXT ext;
};

// Directory entry — identical layout to FAT12/16
struct __attribute__((packed)) FAT32_DirEntry {
    pt::uint8_t  filename[8];
    pt::uint8_t  extension[3];
    pt::uint8_t  attributes;
    pt::uint8_t  reserved;
    pt::uint8_t  create_time_tenths;
    pt::uint16_t create_time;
    pt::uint16_t create_date;
    pt::uint16_t access_date;
    pt::uint16_t first_cluster_high;   // high 16 bits of start cluster
    pt::uint16_t modify_time;
    pt::uint16_t modify_date;
    pt::uint16_t first_cluster_low;
    pt::uint32_t file_size;
};

// Long File Name directory entry
struct __attribute__((packed)) FAT32_LFN_Entry {
    pt::uint8_t  seq_num;        // bit[5:0] = seq (1-based); bit6 = last-in-sequence
    pt::uint16_t name1[5];       // UTF-16LE chars 1–5
    pt::uint8_t  attributes;     // 0x0F
    pt::uint8_t  type;           // 0
    pt::uint8_t  checksum;       // checksum of the 8.3 entry that follows
    pt::uint16_t name2[6];       // UTF-16LE chars 6–11
    pt::uint16_t reserved;       // 0
    pt::uint16_t name3[2];       // UTF-16LE chars 12–13
};

#define FAT32_ATTR_READ_ONLY  0x01
#define FAT32_ATTR_HIDDEN     0x02
#define FAT32_ATTR_SYSTEM     0x04
#define FAT32_ATTR_VOLUME_ID  0x08
#define FAT32_ATTR_DIRECTORY  0x10
#define FAT32_ATTR_ARCHIVE    0x20
#define FAT32_ATTR_LFN        0x0F   // all four low attr bits set

// FAT32-private state stored in File::fs_data (must be <= 32 bytes)
struct FAT32State {
    pt::uint32_t first_cluster;        // first cluster of the file (constant)
    pt::uint32_t current_cluster;      // cluster reached after last operation
    pt::uint32_t current_cluster_idx;  // which cluster index current_cluster is
    pt::uint32_t dir_entry_sector;     // disk sector holding this file's dir entry
    pt::uint16_t dir_entry_offset;     // byte offset of the entry within that sector
    pt::uint8_t  _pad[2];
};
static_assert(sizeof(FAT32State) <= 32, "FAT32State overflows File::fs_data");

class FAT32 : public Filesystem {
public:
    bool mount() override;
    bool open_file(const char* filename, File* file) override;
    pt::uint32_t read_file(File* file, void* buffer, pt::uint32_t bytes_to_read) override;
    pt::uint32_t write_file(File* file, const void* buffer, pt::uint32_t bytes_to_write) override;
    pt::uint32_t seek_file(File* file, pt::int32_t offset, int whence) override;
    void close_file(File* file) override;
    bool file_exists(const char* filename) override;
    void list_root_directory() override;
    bool open_file_write(const char* filename, File* out) override;
    bool create_file(const char* filename, const pt::uint8_t* data, pt::uint32_t size) override;
    bool delete_file(const char* filename) override;
    pt::uint32_t get_bytes_per_cluster() override;
    pt::uint32_t get_free_space() override;
    pt::uint32_t get_total_space() override;

private:
    pt::uint32_t get_next_cluster(pt::uint32_t cluster);
    pt::uint32_t cluster_to_sector(pt::uint32_t cluster);
    void format_filename_83(const char* input, char* output);
    bool compare_filename(const FAT32_DirEntry* entry,
                          const char* lfn_buf,
                          const char* filename);

    // Walk a directory cluster chain; if filename!=nullptr search for it,
    // else list all entries.  Returns true when a match is found.
    bool scan_directory(pt::uint32_t start_cluster,
                        const char*  filename,
                        File*        out);

    pt::uint32_t allocate_cluster();
    bool write_fat_entry(pt::uint32_t cluster, pt::uint32_t value);
    bool update_dir_entry(File* file);
    bool create_dir_entry(const char* filename, File* out);

    FAT32_BPB    bpb;
    pt::uint32_t* fat_table         = nullptr;  // cached FAT (array of uint32)
    pt::uint32_t  fat_sectors        = 0;
    pt::uint32_t  fat_start_sector   = 0;
    pt::uint32_t  data_start_sector  = 0;
    pt::uint32_t  total_clusters     = 0;
    pt::uint32_t  root_cluster       = 0;
    bool          mounted            = false;
};
