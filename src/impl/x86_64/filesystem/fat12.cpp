#include "fat12.h"
#include "disk.h"
#include "kernel.h"
#include "virtual.h"

extern VMM vmm;

FAT12_BPB FAT12::bpb;
pt::uint8_t* FAT12::fat_table = nullptr;
pt::uint32_t FAT12::fat_start_sector = 0;
pt::uint32_t FAT12::root_dir_start_sector = 0;
pt::uint32_t FAT12::data_start_sector = 0;
pt::uint32_t FAT12::root_dir_sectors = 0;
pt::uint32_t FAT12::total_clusters = 0;
bool FAT12::mounted = false;

// Global sector buffer to avoid stack issues - aligned to 4 bytes for DMA safety
static pt::uint8_t sector_buffer[512] __attribute__((aligned(4)));

bool FAT12::initialize() {
    if (mounted) {
        return true;
    }
    
    klog("[FAT12] Initializing...\n");
    
    if (!Disk::is_present()) {
        klog("[FAT12] No disk present, skipping FAT12 mount\n");
        return false;
    }
    
    klog("[FAT12] Reading boot sector...\n");
    
    // Read boot sector (sector 0) - retry up to 3 times
    bool read_ok = false;
    for (int retry = 0; retry < 3; retry++) {
        if (Disk::read_sector(0, sector_buffer)) {
            read_ok = true;
            break;
        }
        klog("[FAT12] Boot sector read failed, retry %d...\n", retry);
    }
    
    if (!read_ok) {
        klog("[FAT12] Failed to read boot sector after retries, skipping mount\n");
        return false;
    }
    
    klog("[FAT12] Boot sector read OK\n");
    
    // Check boot signature
    if (sector_buffer[510] != 0x55 || sector_buffer[511] != 0xAA) {
        klog("[FAT12] Invalid boot signature: %x %x, skipping mount\n", 
             sector_buffer[510], sector_buffer[511]);
        return false;
    }
    
    klog("[FAT12] Boot signature OK\n");
    
    // Copy BPB
    memcpy(&bpb, sector_buffer, sizeof(FAT12_BPB));
    
    // Validate BPB fields
    if (bpb.bytes_per_sector == 0 || bpb.bytes_per_sector > 4096 ||
        bpb.bytes_per_sector % 512 != 0) {
        klog("[FAT12] Invalid bytes_per_sector: %d, skipping mount\n", bpb.bytes_per_sector);
        return false;
    }
    
    if (bpb.sectors_per_cluster == 0 || bpb.sectors_per_cluster > 128) {
        klog("[FAT12] Invalid sectors_per_cluster: %d, skipping mount\n", bpb.sectors_per_cluster);
        return false;
    }
    
    if (bpb.fat_count == 0 || bpb.fat_count > 4) {
        klog("[FAT12] Invalid fat_count: %d, skipping mount\n", bpb.fat_count);
        return false;
    }
    
    if (bpb.sectors_per_fat == 0 || bpb.sectors_per_fat > 256) {
        klog("[FAT12] Invalid sectors_per_fat: %d, skipping mount\n", bpb.sectors_per_fat);
        return false;
    }
    
    klog("[FAT12] BPB validated OK\n");
    
    // Calculate filesystem layout
    fat_start_sector = bpb.reserved_sector_count;
    root_dir_sectors = ((bpb.root_entry_count * 32) + (bpb.bytes_per_sector - 1)) / bpb.bytes_per_sector;
    root_dir_start_sector = fat_start_sector + (bpb.fat_count * bpb.sectors_per_fat);
    data_start_sector = root_dir_start_sector + root_dir_sectors;
    
    pt::uint32_t total_sectors = (bpb.total_sectors_16 != 0) ? bpb.total_sectors_16 : bpb.total_sectors_32;
    pt::uint32_t data_sectors = total_sectors - data_start_sector;
    total_clusters = data_sectors / bpb.sectors_per_cluster;
    
    klog("[FAT12] Layout: FAT@%d, Root@%d, Data@%d, Clusters=%d\n", 
         fat_start_sector, root_dir_start_sector, data_start_sector, total_clusters);
    
    // Allocate and read FAT
    pt::uint32_t fat_size = bpb.sectors_per_fat * bpb.bytes_per_sector;
    klog("[FAT12] Allocating %d bytes for FAT...\n", fat_size);
    
    fat_table = (pt::uint8_t*)vmm.kmalloc(fat_size);
    if (!fat_table) {
        klog("[FAT12] Failed to allocate FAT table, skipping mount\n");
        return false;
    }
    
    klog("[FAT12] Reading FAT (%d sectors)...\n", bpb.sectors_per_fat);
    bool fat_ok = true;
    for (pt::uint32_t i = 0; i < bpb.sectors_per_fat; i++) {
        if (!Disk::read_sector(fat_start_sector + i, fat_table + (i * bpb.bytes_per_sector))) {
            klog("[FAT12] Warning: Failed to read FAT sector %d\n", i);
            fat_ok = false;
        }
    }
    
    if (!fat_ok) {
        klog("[FAT12] FAT read had errors, but continuing...\n");
    }
    
    mounted = true;
    klog("[FAT12] Mounted successfully\n");
    return true;
}

pt::uint16_t FAT12::get_next_cluster(pt::uint16_t cluster) {
    if (!mounted || !fat_table || cluster < 2 || cluster >= total_clusters) {
        return 0xFFF;
    }
    
    pt::uint32_t fat_offset = cluster + (cluster / 2);
    pt::uint16_t fat_value = *(pt::uint16_t*)(fat_table + fat_offset);
    
    if (cluster & 1) {
        return fat_value >> 4;
    } else {
        return fat_value & 0x0FFF;
    }
}

pt::uint32_t FAT12::cluster_to_sector(pt::uint16_t cluster) {
    if (cluster < 2) return 0;
    return data_start_sector + ((cluster - 2) * bpb.sectors_per_cluster);
}

void FAT12::format_filename(const char* input, char* output) {
    int i = 0, j = 0;
    
    // Name part (up to 8 chars)
    while (input[i] && input[i] != '.' && j < 8) {
        char c = input[i];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        output[j++] = c;
        i++;
    }
    
    while (j < 8) output[j++] = ' ';
    
    // Extension
    if (input[i] == '.') i++;
    while (input[i] && j < 11) {
        char c = input[i];
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        output[j++] = c;
        i++;
    }
    
    while (j < 11) output[j++] = ' ';
    output[11] = '\0';
}

bool FAT12::compare_filename(const FAT12_DirEntry* entry, const char* filename) {
    char formatted[12];
    format_filename(filename, formatted);
    
    for (int i = 0; i < 11; i++) {
        if (entry->filename[i] != formatted[i]) {
            return false;
        }
    }
    return true;
}

bool FAT12::file_exists(const char* filename) {
    if (!mounted) return false;
    FAT12_File dummy;
    return open_file(filename, &dummy);
}

bool FAT12::open_file(const char* filename, FAT12_File* file) {
    if (!mounted) {
        return false;
    }
    
    pt::uint32_t entries_per_sector = bpb.bytes_per_sector / 32;
    
    for (pt::uint32_t s = 0; s < root_dir_sectors; s++) {
        if (!Disk::read_sector(root_dir_start_sector + s, sector_buffer)) {
            continue;
        }
        
        FAT12_DirEntry* entries = (FAT12_DirEntry*)sector_buffer;
        
        for (pt::uint32_t e = 0; e < entries_per_sector; e++) {
            if (entries[e].filename[0] == 0x00) {
                return false;
            }
            
            if (entries[e].filename[0] == 0xE5) {
                continue;
            }
            
            if (entries[e].attributes & 0x08) continue;
            if (entries[e].attributes & 0x10) continue;
            
            if (compare_filename(&entries[e], filename)) {
                file->file_size = entries[e].file_size;
                file->current_position = 0;
                file->current_cluster = entries[e].first_cluster_low;
                file->start_sector = cluster_to_sector(file->current_cluster);
                file->open = true;
                
                int k = 0;
                for (int i = 0; i < 8 && entries[e].filename[i] != ' '; i++) {
                    file->filename[k++] = entries[e].filename[i];
                }
                if (entries[e].extension[0] != ' ') {
                    file->filename[k++] = '.';
                    for (int i = 0; i < 3 && entries[e].extension[i] != ' '; i++) {
                        file->filename[k++] = entries[e].extension[i];
                    }
                }
                file->filename[k] = '\0';
                
                return true;
            }
        }
    }
    
    return false;
}

pt::uint32_t FAT12::read_file(FAT12_File* file, void* buffer, pt::uint32_t bytes_to_read) {
    if (!mounted || !file || !file->open) {
        return 0;
    }
    
    pt::uint32_t bytes_remaining = file->file_size - file->current_position;
    if (bytes_to_read > bytes_remaining) {
        bytes_to_read = bytes_remaining;
    }
    
    if (bytes_to_read == 0) {
        return 0;
    }
    
    pt::uint8_t* out = (pt::uint8_t*)buffer;
    pt::uint32_t bytes_read = 0;
    pt::uint32_t bytes_per_cluster = bpb.sectors_per_cluster * bpb.bytes_per_sector;
    
    pt::uint32_t cluster_offset = file->current_position / bytes_per_cluster;
    pt::uint32_t byte_offset = file->current_position % bytes_per_cluster;
    
    pt::uint16_t current_cluster = file->current_cluster;
    for (pt::uint32_t i = 0; i < cluster_offset && current_cluster < 0xFF8; i++) {
        current_cluster = get_next_cluster(current_cluster);
    }
    
    while (bytes_read < bytes_to_read && current_cluster < 0xFF8) {
        pt::uint32_t sector = cluster_to_sector(current_cluster);
        pt::uint32_t sector_offset = byte_offset / bpb.bytes_per_sector;
        pt::uint32_t sector_byte = byte_offset % bpb.bytes_per_sector;
        
        for (pt::uint32_t s = sector_offset; 
             s < bpb.sectors_per_cluster && bytes_read < bytes_to_read; 
             s++) {
            
            if (!Disk::read_sector(sector + s, sector_buffer)) {
                file->current_position += bytes_read;
                file->current_cluster = current_cluster;
                return bytes_read;
            }
            
            pt::uint32_t bytes_from_sector = bpb.bytes_per_sector - sector_byte;
            if (bytes_from_sector > bytes_to_read - bytes_read) {
                bytes_from_sector = bytes_to_read - bytes_read;
            }
            
            memcpy(out + bytes_read, sector_buffer + sector_byte, bytes_from_sector);
            bytes_read += bytes_from_sector;
            sector_byte = 0;
        }
        
        byte_offset = 0;
        
        if (bytes_read < bytes_to_read) {
            current_cluster = get_next_cluster(current_cluster);
        }
    }
    
    file->current_position += bytes_read;
    file->current_cluster = current_cluster;
    return bytes_read;
}

void FAT12::close_file(FAT12_File* file) {
    if (file) {
        file->open = false;
    }
}

void FAT12::list_root_directory() {
    if (!mounted) {
        klog("[FAT12] Not mounted\n");
        return;
    }
    
    klog("[FAT12] Root directory:\n");
    
    pt::uint32_t entries_per_sector = bpb.bytes_per_sector / 32;
    int file_count = 0;
    
    for (pt::uint32_t s = 0; s < root_dir_sectors; s++) {
        if (!Disk::read_sector(root_dir_start_sector + s, sector_buffer)) {
            continue;
        }
        
        FAT12_DirEntry* entries = (FAT12_DirEntry*)sector_buffer;
        
        for (pt::uint32_t e = 0; e < entries_per_sector; e++) {
            if (entries[e].filename[0] == 0x00) {
                s = root_dir_sectors;
                break;
            }
            
            if (entries[e].filename[0] == 0xE5) {
                continue;
            }
            
            char name[13];
            int k = 0;
            for (int i = 0; i < 8 && entries[e].filename[i] != ' '; i++) {
                name[k++] = entries[e].filename[i];
            }
            if (entries[e].extension[0] != ' ') {
                name[k++] = '.';
                for (int i = 0; i < 3 && entries[e].extension[i] != ' '; i++) {
                    name[k++] = entries[e].extension[i];
                }
            }
            name[k] = '\0';
            
            const char* type = (entries[e].attributes & 0x10) ? "DIR" : "FILE";
            klog("  %s %s %d bytes\n", type, name, entries[e].file_size);
            file_count++;
        }
    }
    
    if (file_count == 0) {
        klog("  (empty)\n");
    }
}

pt::uint32_t FAT12::get_bytes_per_cluster() {
    if (!mounted) return 0;
    return bpb.sectors_per_cluster * bpb.bytes_per_sector;
}

pt::uint32_t FAT12::get_free_space() {
    if (!mounted || !fat_table) return 0;
    
    pt::uint32_t free_clusters = 0;
    for (pt::uint32_t i = 2; i < total_clusters; i++) {
        if (get_next_cluster(i) == 0) {
            free_clusters++;
        }
    }
    
    return free_clusters * get_bytes_per_cluster();
}

pt::uint32_t FAT12::get_total_space() {
    if (!mounted) return 0;
    return (total_clusters - 2) * get_bytes_per_cluster();
}
