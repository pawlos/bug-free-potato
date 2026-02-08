#pragma once

#include "defs.h"

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

// File handle for open files
struct FAT12_File {
    char filename[13];  // 8.3 + null terminator
    pt::uint32_t file_size;
    pt::uint32_t current_position;
    pt::uint16_t current_cluster;
    pt::uint32_t start_sector;
    pt::uint8_t  buffer[512];
    bool open;
};

class FAT12 {
public:
    static bool initialize();
    static bool open_file(const char* filename, FAT12_File* file);
    static pt::uint32_t read_file(FAT12_File* file, void* buffer, pt::uint32_t bytes_to_read);
    static void close_file(FAT12_File* file);
    static bool file_exists(const char* filename);
    static void list_root_directory();

    // Filesystem info getters
    static pt::uint32_t get_bytes_per_cluster();
    static pt::uint32_t get_free_space();
    static pt::uint32_t get_total_space();

private:
    static bool read_bpb();
    static bool read_fat();
    static pt::uint16_t get_next_cluster(pt::uint16_t cluster);
    static pt::uint32_t cluster_to_sector(pt::uint16_t cluster);
    static bool compare_filename(const FAT12_DirEntry* entry, const char* filename);
    static void format_filename(const char* input, char* output);
    
    static FAT12_BPB bpb;
    static pt::uint8_t* fat_table;
    static pt::uint32_t fat_start_sector;
    static pt::uint32_t root_dir_start_sector;
    static pt::uint32_t data_start_sector;
    static pt::uint32_t root_dir_sectors;
    static pt::uint32_t total_clusters;
    static bool mounted;
};
