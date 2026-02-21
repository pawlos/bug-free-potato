#pragma once

#include "defs.h"
#include "vfs.h"

// FAT12 Filesystem structures and API

// BIOS Parameter Block (first 36 bytes of boot sector)
struct __attribute__((packed)) FAT12_BPB {
    pt::uint8_t  boot_jump[3];
    pt::uint8_t  oem_name[8];
    pt::uint16_t bytes_per_sector;
    pt::uint8_t  sectors_per_cluster;
    pt::uint16_t reserved_sector_count;
    pt::uint8_t  fat_count;
    pt::uint16_t root_entry_count;
    pt::uint16_t total_sectors_16;
    pt::uint8_t  media_type;
    pt::uint16_t sectors_per_fat;
    pt::uint16_t sectors_per_track;
    pt::uint16_t head_count;
    pt::uint32_t hidden_sector_count;
    pt::uint32_t total_sectors_32;
    pt::uint8_t  drive_number;
    pt::uint8_t  reserved1;
    pt::uint8_t  boot_signature;
    pt::uint32_t volume_serial;
    pt::uint8_t  volume_label[11];
    pt::uint8_t  filesystem_type[8];
};

// Directory entry (32 bytes)
struct __attribute__((packed)) FAT12_DirEntry {
    pt::uint8_t  filename[8];
    pt::uint8_t  extension[3];
    pt::uint8_t  attributes;
    pt::uint8_t  reserved;
    pt::uint8_t  create_time_tenths;
    pt::uint16_t create_time;
    pt::uint16_t create_date;
    pt::uint16_t access_date;
    pt::uint16_t first_cluster_high;
    pt::uint16_t modify_time;
    pt::uint16_t modify_date;
    pt::uint16_t first_cluster_low;
    pt::uint32_t file_size;
};

// FAT12-private state stored in File::fs_data
struct FAT12State {
    pt::uint16_t current_cluster;
    pt::uint32_t start_sector;
};
static_assert(sizeof(FAT12State) <= 32, "FAT12State overflows fs_data");

class FAT12 : public Filesystem {
public:
    bool mount() override;
    bool open_file(const char* filename, File* file) override;
    pt::uint32_t read_file(File* file, void* buffer, pt::uint32_t bytes_to_read) override;
    void close_file(File* file) override;
    bool file_exists(const char* filename) override;
    void list_root_directory() override;
    bool create_file(const char* filename, const pt::uint8_t* data, pt::uint32_t size) override;
    bool delete_file(const char* filename) override;

    // Filesystem info getters
    pt::uint32_t get_bytes_per_cluster() override;
    pt::uint32_t get_free_space() override;
    pt::uint32_t get_total_space() override;

private:
    bool read_bpb();
    bool read_fat();
    pt::uint16_t get_next_cluster(pt::uint16_t cluster);
    pt::uint32_t cluster_to_sector(pt::uint16_t cluster);
    bool compare_filename(const FAT12_DirEntry* entry, const char* filename);
    void format_filename(const char* input, char* output);
    pt::uint16_t find_free_cluster(pt::uint16_t start_from);
    void set_fat_entry(pt::uint16_t cluster, pt::uint16_t value);
    bool flush_fat();

    FAT12_BPB bpb;
    pt::uint8_t* fat_table = nullptr;
    pt::uint32_t fat_start_sector = 0;
    pt::uint32_t root_dir_start_sector = 0;
    pt::uint32_t data_start_sector = 0;
    pt::uint32_t root_dir_sectors = 0;
    pt::uint32_t total_clusters = 0;
    bool mounted = false;
};
