#include "fat32.h"
#include "disk.h"
#include "kernel.h"
#include "virtual.h"

extern VMM vmm;

// Sector-sized scratch buffer (aligned for DMA safety, off the stack)
static pt::uint8_t fat32_sector_buf[512] __attribute__((aligned(4)));

static inline FAT32State* fat32_state(File* f) {
    return reinterpret_cast<FAT32State*>(f->fs_data);
}

// ── Cluster helpers ───────────────────────────────────────────────────────

static inline bool is_eoc(pt::uint32_t cluster) {
    return (cluster & 0x0FFFFFFF) >= 0x0FFFFFF8u;
}

static inline char to_upper(char c) {
    return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
}

// Case-insensitive ASCII strcmp (returns true when equal)
static bool ci_streq(const char* a, const char* b) {
    while (*a && *b) {
        if (to_upper(*a) != to_upper(*b)) return false;
        a++; b++;
    }
    return *a == '\0' && *b == '\0';
}

// ── mount ─────────────────────────────────────────────────────────────────

bool FAT32::mount() {
    if (mounted) return true;

    klog("[FAT32] Initializing...\n");

    if (!Disk::is_present()) {
        klog("[FAT32] No disk present\n");
        return false;
    }

    bool ok = false;
    for (int retry = 0; retry < 3; retry++) {
        if (Disk::read_sector(0, fat32_sector_buf)) { ok = true; break; }
        klog("[FAT32] Boot sector read failed, retry %d\n", retry);
    }
    if (!ok) { klog("[FAT32] Failed to read boot sector\n"); return false; }

    if (fat32_sector_buf[510] != 0x55 || fat32_sector_buf[511] != 0xAA) {
        klog("[FAT32] Invalid boot signature\n");
        return false;
    }

    memcpy(&bpb, fat32_sector_buf, sizeof(FAT32_BPB));

    if (bpb.bytes_per_sector == 0 || bpb.sectors_per_cluster == 0 ||
        bpb.fat_count == 0 || bpb.ext.sectors_per_fat_32 == 0) {
        klog("[FAT32] Invalid BPB fields\n");
        return false;
    }

    fat_sectors       = bpb.ext.sectors_per_fat_32;
    root_cluster      = bpb.ext.root_cluster;
    fat_start_sector  = bpb.reserved_sector_count;
    data_start_sector = fat_start_sector + bpb.fat_count * fat_sectors;
    // root_entry_count == 0 for FAT32, so no separate root dir region

    pt::uint32_t total_sectors = bpb.total_sectors_32;
    pt::uint32_t data_sectors  = (total_sectors > data_start_sector)
                                 ? total_sectors - data_start_sector : 0;
    total_clusters = data_sectors / bpb.sectors_per_cluster;

    klog("[FAT32] FAT@%d (%d sectors), Data@%d, Clusters=%d, RootCluster=%d\n",
         fat_start_sector, fat_sectors, data_start_sector,
         total_clusters, root_cluster);

    // Cache the entire FAT in kernel heap
    pt::uint32_t fat_bytes = fat_sectors * bpb.bytes_per_sector;
    fat_table = (pt::uint32_t*)vmm.kmalloc(fat_bytes);
    if (!fat_table) {
        klog("[FAT32] Failed to allocate FAT cache (%d bytes)\n", fat_bytes);
        return false;
    }

    pt::uint8_t* fat_raw = (pt::uint8_t*)fat_table;
    for (pt::uint32_t i = 0; i < fat_sectors; i++) {
        if (!Disk::read_sector(fat_start_sector + i,
                               fat_raw + i * bpb.bytes_per_sector)) {
            klog("[FAT32] Warning: failed to read FAT sector %d\n", i);
        }
    }

    mounted = true;
    klog("[FAT32] Mounted successfully\n");
    return true;
}

// ── Cluster navigation ────────────────────────────────────────────────────

pt::uint32_t FAT32::get_next_cluster(pt::uint32_t cluster) {
    if (!fat_table || cluster < 2 || cluster >= total_clusters + 2)
        return 0x0FFFFFFF;
    return fat_table[cluster] & 0x0FFFFFFF;
}

pt::uint32_t FAT32::cluster_to_sector(pt::uint32_t cluster) {
    if (cluster < 2) return data_start_sector;
    return data_start_sector + (cluster - 2) * bpb.sectors_per_cluster;
}

// ── 8.3 name formatting ───────────────────────────────────────────────────

void FAT32::format_filename_83(const char* input, char* output) {
    int i = 0, j = 0;
    while (input[i] && input[i] != '.' && j < 8)
        output[j++] = to_upper(input[i++]);
    while (j < 8) output[j++] = ' ';
    if (input[i] == '.') i++;
    while (input[i] && j < 11)
        output[j++] = to_upper(input[i++]);
    while (j < 11) output[j++] = ' ';
    output[11] = '\0';
}

// ── Filename comparison ───────────────────────────────────────────────────

bool FAT32::compare_filename(const FAT32_DirEntry* entry,
                             const char* lfn_buf,
                             const char* filename) {
    // Prefer LFN match (case-insensitive)
    if (lfn_buf && lfn_buf[0] != '\0' && ci_streq(lfn_buf, filename))
        return true;

    // Fall back to 8.3
    char formatted[12];
    format_filename_83(filename, formatted);
    for (int i = 0; i < 11; i++) {
        if (entry->filename[i] != (pt::uint8_t)formatted[i]) return false;
    }
    return true;
}

// ── LFN accumulation ──────────────────────────────────────────────────────
// LFN entries are stored on disk in *reverse* order (highest seq first).
// seq is 1-based; the first 13 chars are seq=1, next 13 are seq=2, etc.
// We store each chunk at position (seq-1)*13 in lfn_buf so the buffer
// assembles correctly regardless of read order.

static void lfn_store_entry(const FAT32_LFN_Entry* lfn,
                             char* lfn_buf, int buf_size) {
    int seq = lfn->seq_num & 0x1F;
    if (seq < 1 || seq > 20) return;
    int base = (seq - 1) * 13;

    // Copy the packed LFN entry to a local array of 13 uint16s so we can
    // read safely without taking addresses of packed struct members.
    pt::uint16_t chars[13];
    __builtin_memcpy(chars + 0, lfn->name1, sizeof(lfn->name1));
    __builtin_memcpy(chars + 5, lfn->name2, sizeof(lfn->name2));
    __builtin_memcpy(chars + 11, lfn->name3, sizeof(lfn->name3));

    for (int i = 0; i < 13; i++) {
        pt::uint16_t ch = chars[i];
        if (ch == 0x0000 || ch == 0xFFFF) {
            if (base + i < buf_size)
                lfn_buf[base + i] = '\0';
            return;
        }
        if (base + i < buf_size - 1)
            lfn_buf[base + i] = (ch < 0x80) ? (char)ch : '?';
    }
}

// ── scan_directory ────────────────────────────────────────────────────────
// Walk every sector of every cluster in the directory chain.
// filename == nullptr → list mode (print all entries via klog).
// filename != nullptr → search mode (fill *out, return true on match).

bool FAT32::scan_directory(pt::uint32_t start_cluster,
                           const char*  filename,
                           File*        out) {
    char lfn_buf[261];   // up to 20 × 13 = 260 chars + NUL
    for (int i = 0; i < (int)sizeof(lfn_buf); i++) lfn_buf[i] = '\0';
    bool searching = (filename != nullptr);

    pt::uint32_t cluster = start_cluster;
    while (cluster >= 2 && !is_eoc(cluster)) {
        pt::uint32_t sector = cluster_to_sector(cluster);

        for (pt::uint8_t s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!Disk::read_sector(sector + s, fat32_sector_buf)) continue;

            pt::uint32_t entries_per_sector = bpb.bytes_per_sector / 32;
            FAT32_DirEntry* entries = (FAT32_DirEntry*)fat32_sector_buf;

            for (pt::uint32_t e = 0; e < entries_per_sector; e++) {
                pt::uint8_t first = entries[e].filename[0];

                if (first == 0x00) return false;   // end of directory
                if (first == 0xE5) {               // deleted — reset LFN
                    lfn_buf[0] = '\0';
                    continue;
                }

                if (entries[e].attributes == FAT32_ATTR_LFN) {
                    const FAT32_LFN_Entry* lfn =
                        (const FAT32_LFN_Entry*)&entries[e];
                    // Bit 6 of seq_num marks the last LFN entry stored
                    // (= first in logical order), so clear buffer on it.
                    if (lfn->seq_num & 0x40) {
                        for (int i = 0; i < (int)sizeof(lfn_buf); i++)
                            lfn_buf[i] = '\0';
                    }
                    lfn_store_entry(lfn, lfn_buf, sizeof(lfn_buf));
                    continue;
                }

                if (entries[e].attributes & FAT32_ATTR_VOLUME_ID) {
                    lfn_buf[0] = '\0';
                    continue;
                }

                if (entries[e].attributes & FAT32_ATTR_DIRECTORY) {
                    // Subdirectory — skip (no recursive open yet)
                    lfn_buf[0] = '\0';
                    continue;
                }

                // Regular file entry
                if (!searching) {
                    // List mode
                    if (lfn_buf[0] != '\0') {
                        klog("  FILE %s %d bytes\n",
                             lfn_buf, entries[e].file_size);
                    } else {
                        char name[13]; int k = 0;
                        for (int i = 0; i < 8 && entries[e].filename[i] != ' '; i++)
                            name[k++] = entries[e].filename[i];
                        if (entries[e].extension[0] != ' ') {
                            name[k++] = '.';
                            for (int i = 0; i < 3 && entries[e].extension[i] != ' '; i++)
                                name[k++] = entries[e].extension[i];
                        }
                        name[k] = '\0';
                        klog("  FILE %s %d bytes\n", name, entries[e].file_size);
                    }
                } else if (compare_filename(&entries[e], lfn_buf, filename)) {
                    // Found it
                    pt::uint32_t first_cluster =
                        ((pt::uint32_t)entries[e].first_cluster_high << 16) |
                        entries[e].first_cluster_low;

                    out->file_size        = entries[e].file_size;
                    out->current_position = 0;
                    out->open             = true;

                    FAT32State* st        = fat32_state(out);
                    st->first_cluster     = first_cluster;
                    st->current_cluster   = first_cluster;
                    st->current_cluster_idx = 0;

                    // Copy 8.3 name into out->filename
                    int k = 0;
                    for (int i = 0; i < 8 && entries[e].filename[i] != ' '; i++)
                        out->filename[k++] = entries[e].filename[i];
                    if (entries[e].extension[0] != ' ') {
                        out->filename[k++] = '.';
                        for (int i = 0; i < 3 && entries[e].extension[i] != ' '; i++)
                            out->filename[k++] = entries[e].extension[i];
                    }
                    out->filename[k] = '\0';
                    return true;
                }

                lfn_buf[0] = '\0';  // consume accumulated LFN after use
            }
        }

        cluster = get_next_cluster(cluster);
    }
    return false;
}

// ── Public Filesystem API ─────────────────────────────────────────────────

bool FAT32::open_file(const char* filename, File* file) {
    if (!mounted) return false;
    return scan_directory(root_cluster, filename, file);
}

bool FAT32::file_exists(const char* filename) {
    if (!mounted) return false;
    File dummy;
    return scan_directory(root_cluster, filename, &dummy);
}

void FAT32::list_root_directory() {
    if (!mounted) { klog("[FAT32] Not mounted\n"); return; }
    klog("[FAT32] Root directory:\n");
    scan_directory(root_cluster, nullptr, nullptr);
}

pt::uint32_t FAT32::read_file(File* file, void* buffer,
                               pt::uint32_t bytes_to_read) {
    if (!mounted || !file || !file->open) return 0;

    pt::uint32_t remaining = file->file_size - file->current_position;
    if (bytes_to_read > remaining) bytes_to_read = remaining;
    if (bytes_to_read == 0) return 0;

    pt::uint8_t* out = (pt::uint8_t*)buffer;
    pt::uint32_t bytes_read      = 0;
    pt::uint32_t bytes_per_cluster =
        (pt::uint32_t)bpb.sectors_per_cluster * bpb.bytes_per_sector;

    pt::uint32_t cluster_idx = file->current_position / bytes_per_cluster;
    pt::uint32_t byte_offset = file->current_position % bytes_per_cluster;

    FAT32State* state = fat32_state(file);
    pt::uint32_t current_cluster;

    if (cluster_idx >= state->current_cluster_idx) {
        // Seek forward from cached position
        current_cluster = state->current_cluster;
        for (pt::uint32_t i = state->current_cluster_idx;
             i < cluster_idx && !is_eoc(current_cluster); i++) {
            current_cluster = get_next_cluster(current_cluster);
        }
    } else {
        // Backward seek — must restart from first cluster
        current_cluster = state->first_cluster;
        for (pt::uint32_t i = 0;
             i < cluster_idx && !is_eoc(current_cluster); i++) {
            current_cluster = get_next_cluster(current_cluster);
        }
    }

    while (bytes_read < bytes_to_read && !is_eoc(current_cluster)) {
        pt::uint32_t sector     = cluster_to_sector(current_cluster);
        pt::uint32_t sec_offset = byte_offset / bpb.bytes_per_sector;
        pt::uint32_t sec_byte   = byte_offset % bpb.bytes_per_sector;

        for (pt::uint32_t s = sec_offset;
             s < bpb.sectors_per_cluster && bytes_read < bytes_to_read;
             s++) {
            if (!Disk::read_sector(sector + s, fat32_sector_buf)) {
                file->current_position += bytes_read;
                state->current_cluster  = current_cluster;
                return bytes_read;
            }

            pt::uint32_t from_sector = bpb.bytes_per_sector - sec_byte;
            if (from_sector > bytes_to_read - bytes_read)
                from_sector = bytes_to_read - bytes_read;

            memcpy(out + bytes_read, fat32_sector_buf + sec_byte, from_sector);
            bytes_read += from_sector;
            sec_byte = 0;
        }

        byte_offset = 0;
        if (bytes_read < bytes_to_read)
            current_cluster = get_next_cluster(current_cluster);
    }

    file->current_position    += bytes_read;
    state->current_cluster     = current_cluster;
    state->current_cluster_idx = file->current_position / bytes_per_cluster;
    return bytes_read;
}

pt::uint32_t FAT32::seek_file(File* file, pt::int32_t offset, int whence) {
    if (!file || !file->open) return (pt::uint32_t)-1;
    pt::uint32_t new_pos;
    switch (whence) {
        case 0: new_pos = (pt::uint32_t)offset; break;
        case 1: new_pos = (pt::uint32_t)((pt::int32_t)file->current_position + offset); break;
        case 2: new_pos = (pt::uint32_t)((pt::int32_t)file->file_size + offset); break;
        default: return (pt::uint32_t)-1;
    }
    if (new_pos > file->file_size) new_pos = file->file_size;
    file->current_position = new_pos;
    // Cluster cache stays valid; read_file will walk forward/backward as needed.
    return new_pos;
}

void FAT32::close_file(File* file) {
    if (file) file->open = false;
}

// Write support not yet implemented
bool FAT32::create_file(const char* filename,
                        const pt::uint8_t* data, pt::uint32_t size) {
    (void)filename; (void)data; (void)size;
    klog("[FAT32] create_file: not implemented\n");
    return false;
}

bool FAT32::delete_file(const char* filename) {
    (void)filename;
    klog("[FAT32] delete_file: not implemented\n");
    return false;
}

// ── Info getters ──────────────────────────────────────────────────────────

pt::uint32_t FAT32::get_bytes_per_cluster() {
    if (!mounted) return 0;
    return (pt::uint32_t)bpb.sectors_per_cluster * bpb.bytes_per_sector;
}

pt::uint32_t FAT32::get_free_space() {
    if (!mounted || !fat_table) return 0;
    pt::uint32_t free_clusters = 0;
    for (pt::uint32_t i = 2; i < total_clusters + 2; i++) {
        if ((fat_table[i] & 0x0FFFFFFF) == 0) free_clusters++;
    }
    return free_clusters * get_bytes_per_cluster();
}

pt::uint32_t FAT32::get_total_space() {
    if (!mounted) return 0;
    return total_clusters * get_bytes_per_cluster();
}
