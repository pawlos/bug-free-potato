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

                    FAT32State* st          = fat32_state(out);
                    st->first_cluster       = first_cluster;
                    st->current_cluster     = first_cluster;
                    st->current_cluster_idx = 0;
                    st->dir_entry_sector    = sector + s;
                    st->dir_entry_offset    = (pt::uint16_t)(e * 32);
                    st->_pad[0] = st->_pad[1] = 0;

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

// Walk root directory and return the idx-th regular file entry.
bool FAT32::readdir(int idx, char* name_out, pt::uint32_t* size_out) {
    if (!mounted) return false;
    int count = 0;
    char lfn_buf[261];
    for (int i = 0; i < (int)sizeof(lfn_buf); i++) lfn_buf[i] = '\0';

    pt::uint32_t cluster = root_cluster;
    while (cluster >= 2 && !is_eoc(cluster)) {
        pt::uint32_t sector = cluster_to_sector(cluster);
        for (pt::uint8_t s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!Disk::read_sector(sector + s, fat32_sector_buf)) continue;
            pt::uint32_t entries_per_sector = bpb.bytes_per_sector / 32;
            FAT32_DirEntry* entries = (FAT32_DirEntry*)fat32_sector_buf;

            for (pt::uint32_t e = 0; e < entries_per_sector; e++) {
                pt::uint8_t first = entries[e].filename[0];
                if (first == 0x00) return false;
                if (first == 0xE5) { lfn_buf[0] = '\0'; continue; }

                if (entries[e].attributes == FAT32_ATTR_LFN) {
                    const FAT32_LFN_Entry* lfn = (const FAT32_LFN_Entry*)&entries[e];
                    if (lfn->seq_num & 0x40) {
                        for (int i = 0; i < (int)sizeof(lfn_buf); i++) lfn_buf[i] = '\0';
                    }
                    lfn_store_entry(lfn, lfn_buf, sizeof(lfn_buf));
                    continue;
                }
                if (entries[e].attributes & FAT32_ATTR_VOLUME_ID) { lfn_buf[0] = '\0'; continue; }
                if (entries[e].attributes & FAT32_ATTR_DIRECTORY) { lfn_buf[0] = '\0'; continue; }

                // Regular file
                if (count == idx) {
                    if (lfn_buf[0] != '\0') {
                        int k = 0;
                        while (lfn_buf[k] && k < 255) { name_out[k] = lfn_buf[k]; k++; }
                        name_out[k] = '\0';
                    } else {
                        int k = 0;
                        for (int i = 0; i < 8 && entries[e].filename[i] != ' '; i++)
                            name_out[k++] = entries[e].filename[i];
                        if (entries[e].extension[0] != ' ') {
                            name_out[k++] = '.';
                            for (int i = 0; i < 3 && entries[e].extension[i] != ' '; i++)
                                name_out[k++] = entries[e].extension[i];
                        }
                        name_out[k] = '\0';
                    }
                    if (size_out) *size_out = entries[e].file_size;
                    return true;
                }
                count++;
                lfn_buf[0] = '\0';
            }
        }
        cluster = get_next_cluster(cluster);
    }
    return false;
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

    file->current_position += bytes_read;
    state->current_cluster = current_cluster;
    // Store the cluster index of the *last byte read* (position-1), not the
    // current position.  If a read ends exactly at a cluster boundary,
    // file->current_position / bytes_per_cluster == next_cluster_idx, but
    // current_cluster still points to the previous cluster.  Subsequent forward
    // navigation would then start from the wrong cluster (off by one), silently
    // re-reading the previous cluster's data instead of the next cluster.
    if (bytes_read > 0)
        state->current_cluster_idx = (file->current_position - 1) / bytes_per_cluster;
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

// ── Write helpers ─────────────────────────────────────────────────────────

// Find a free cluster in the FAT, mark it EOC, flush the FAT sector to disk.
// Returns the cluster number, or 0 on failure (disk full).
pt::uint32_t FAT32::allocate_cluster()
{
    for (pt::uint32_t c = 2; c < total_clusters + 2; c++) {
        if ((fat_table[c] & 0x0FFFFFFF) == 0) {
            write_fat_entry(c, 0x0FFFFFFF);  // marks as EOC
            return c;
        }
    }
    klog("[FAT32] allocate_cluster: disk full\n");
    return 0;
}

// Update one FAT entry in memory and flush the affected FAT sector (both copies).
bool FAT32::write_fat_entry(pt::uint32_t cluster, pt::uint32_t value)
{
    if (!fat_table || cluster < 2 || cluster >= total_clusters + 2) return false;
    fat_table[cluster] = (fat_table[cluster] & 0xF0000000) | (value & 0x0FFFFFFF);

    pt::uint32_t eps         = bpb.bytes_per_sector / 4;   // FAT entries per sector
    pt::uint32_t fat_sec_idx = cluster / eps;
    const pt::uint8_t* data  = (const pt::uint8_t*)fat_table
                               + fat_sec_idx * bpb.bytes_per_sector;
    for (pt::uint8_t i = 0; i < bpb.fat_count; i++) {
        pt::uint32_t lba = fat_start_sector + i * fat_sectors + fat_sec_idx;
        if (!Disk::write_sector(lba, data)) {
            klog("[FAT32] write_fat_entry: write failed lba=%d\n", lba);
            return false;
        }
    }
    return true;
}

// Read the directory sector, patch the size and first-cluster fields, write back.
bool FAT32::update_dir_entry(File* file)
{
    FAT32State* st = fat32_state(file);
    if (st->dir_entry_sector == 0) return false;

    if (!Disk::read_sector(st->dir_entry_sector, fat32_sector_buf)) return false;
    FAT32_DirEntry* e = (FAT32_DirEntry*)(fat32_sector_buf + st->dir_entry_offset);
    e->file_size          = file->file_size;
    e->first_cluster_high = (pt::uint16_t)(st->first_cluster >> 16);
    e->first_cluster_low  = (pt::uint16_t)(st->first_cluster & 0xFFFF);
    return Disk::write_sector(st->dir_entry_sector, fat32_sector_buf);
}

// Find a free (or deleted) slot in the root directory and write a new 8.3 entry.
bool FAT32::create_dir_entry(const char* filename, File* out)
{
    char fmt[12];
    format_filename_83(filename, fmt);

    pt::uint32_t cluster = root_cluster;
    while (cluster >= 2 && !is_eoc(cluster)) {
        pt::uint32_t sector = cluster_to_sector(cluster);
        for (pt::uint8_t s = 0; s < bpb.sectors_per_cluster; s++) {
            if (!Disk::read_sector(sector + s, fat32_sector_buf)) continue;
            pt::uint32_t eps = bpb.bytes_per_sector / 32;
            FAT32_DirEntry* ents = (FAT32_DirEntry*)fat32_sector_buf;
            for (pt::uint32_t e = 0; e < eps; e++) {
                pt::uint8_t first = ents[e].filename[0];
                if (first != 0x00 && first != 0xE5) continue;

                // Found a free slot — initialise it
                memset(&ents[e], 0, 32);
                for (int i = 0; i < 8; i++) ents[e].filename[i]  = (pt::uint8_t)fmt[i];
                for (int i = 0; i < 3; i++) ents[e].extension[i] = (pt::uint8_t)fmt[8 + i];
                ents[e].attributes = 0x20;   // Archive flag

                if (!Disk::write_sector(sector + s, fat32_sector_buf)) return false;

                out->file_size        = 0;
                out->current_position = 0;
                out->open             = true;
                FAT32State* st        = fat32_state(out);
                st->first_cluster     = 0;
                st->current_cluster   = 0;
                st->current_cluster_idx = 0;
                st->dir_entry_sector  = sector + s;
                st->dir_entry_offset  = (pt::uint16_t)(e * 32);
                st->_pad[0] = st->_pad[1] = 0;

                int k = 0;
                for (int i = 0; i < 8 && fmt[i] != ' '; i++) out->filename[k++] = fmt[i];
                if (fmt[8] != ' ') {
                    out->filename[k++] = '.';
                    for (int i = 8; i < 11 && fmt[i] != ' '; i++) out->filename[k++] = fmt[i];
                }
                out->filename[k] = '\0';
                return true;
            }
        }
        cluster = get_next_cluster(cluster);
    }
    klog("[FAT32] create_dir_entry: no free directory slot\n");
    return false;
}

// ── open_file_write ───────────────────────────────────────────────────────
// Create the file if it doesn't exist; truncate it if it does.
bool FAT32::open_file_write(const char* filename, File* out)
{
    if (!mounted) return false;

    if (scan_directory(root_cluster, filename, out)) {
        // File exists — truncate: free every cluster in its chain
        FAT32State* st = fat32_state(out);
        pt::uint32_t c = st->first_cluster;
        while (c >= 2 && !is_eoc(c)) {
            pt::uint32_t next = get_next_cluster(c);
            write_fat_entry(c, 0);   // mark as free
            c = next;
        }
        st->first_cluster       = 0;
        st->current_cluster     = 0;
        st->current_cluster_idx = 0;
        out->file_size          = 0;
        out->current_position   = 0;
        update_dir_entry(out);
        return true;
    }

    return create_dir_entry(filename, out);
}

// ── write_file ────────────────────────────────────────────────────────────
pt::uint32_t FAT32::write_file(File* file, const void* buffer,
                                pt::uint32_t bytes_to_write)
{
    if (!mounted || !file || !file->open || bytes_to_write == 0) return 0;

    const pt::uint8_t* src = (const pt::uint8_t*)buffer;
    pt::uint32_t bytes_written = 0;
    pt::uint32_t bpc   = (pt::uint32_t)bpb.sectors_per_cluster * bpb.bytes_per_sector;
    FAT32State*  state = fat32_state(file);

    pt::uint32_t cluster_idx = file->current_position / bpc;
    pt::uint32_t byte_offset = file->current_position % bpc;
    pt::uint32_t current_cluster = 0;

    // Navigate to (or allocate) the starting cluster
    if (state->first_cluster == 0) {
        current_cluster = allocate_cluster();
        if (current_cluster == 0) return 0;
        state->first_cluster       = current_cluster;
        state->current_cluster     = current_cluster;
        state->current_cluster_idx = 0;
        update_dir_entry(file);
    } else if (cluster_idx >= state->current_cluster_idx) {
        current_cluster = state->current_cluster;
        for (pt::uint32_t i = state->current_cluster_idx; i < cluster_idx; i++) {
            pt::uint32_t next = get_next_cluster(current_cluster);
            if (is_eoc(next) || next < 2) {
                next = allocate_cluster();
                if (next == 0) return bytes_written;
                write_fat_entry(current_cluster, next);
            }
            current_cluster = next;
        }
    } else {
        current_cluster = state->first_cluster;
        for (pt::uint32_t i = 0; i < cluster_idx; i++)
            current_cluster = get_next_cluster(current_cluster);
    }

    // Write loop
    while (bytes_written < bytes_to_write) {
        pt::uint32_t sector   = cluster_to_sector(current_cluster);
        pt::uint32_t sec_off  = byte_offset / bpb.bytes_per_sector;
        pt::uint32_t sec_byte = byte_offset % bpb.bytes_per_sector;
        bool err = false;

        for (pt::uint32_t s = sec_off;
             s < (pt::uint32_t)bpb.sectors_per_cluster && bytes_written < bytes_to_write;
             s++) {
            pt::uint32_t avail    = (pt::uint32_t)bpb.bytes_per_sector - sec_byte;
            pt::uint32_t to_write = bytes_to_write - bytes_written;
            if (to_write > avail) to_write = avail;

            // Partial sector: read first so we don't clobber surrounding bytes
            if (sec_byte != 0 || to_write < (pt::uint32_t)bpb.bytes_per_sector)
                Disk::read_sector(sector + s, fat32_sector_buf);

            memcpy(fat32_sector_buf + sec_byte, src + bytes_written, to_write);
            if (!Disk::write_sector(sector + s, fat32_sector_buf)) { err = true; break; }

            bytes_written += to_write;
            sec_byte = 0;
        }
        if (err) break;

        byte_offset = 0;
        if (bytes_written < bytes_to_write) {
            pt::uint32_t next = get_next_cluster(current_cluster);
            if (is_eoc(next) || next < 2) {
                next = allocate_cluster();
                if (next == 0) break;
                write_fat_entry(current_cluster, next);
            }
            current_cluster = next;
        }
    }

    // Update state
    file->current_position += bytes_written;
    if (file->current_position > file->file_size)
        file->file_size = file->current_position;
    if (bytes_written > 0) {
        state->current_cluster     = current_cluster;
        state->current_cluster_idx = (file->current_position - 1) / bpc;
        update_dir_entry(file);
    }
    return bytes_written;
}

// create_file: one-shot bulk create (used by kernel-internal callers).
bool FAT32::create_file(const char* filename,
                        const pt::uint8_t* data, pt::uint32_t size)
{
    File tmp;
    if (!open_file_write(filename, &tmp)) return false;
    if (data && size) write_file(&tmp, data, size);
    close_file(&tmp);
    return true;
}

bool FAT32::delete_file(const char* filename) {
    if (!mounted) return false;

    File tmp;
    if (!scan_directory(root_cluster, filename, &tmp)) {
        klog("[FAT32] delete_file: '%s' not found\n", filename);
        return false;
    }

    FAT32State* st = fat32_state(&tmp);

    // 1. Free every cluster in the file's chain
    pt::uint32_t c = st->first_cluster;
    while (c >= 2 && !is_eoc(c)) {
        pt::uint32_t next = get_next_cluster(c);
        write_fat_entry(c, 0);
        c = next;
    }

    // 2. Read the directory sector holding the 8.3 entry
    if (!Disk::read_sector(st->dir_entry_sector, fat32_sector_buf)) {
        klog("[FAT32] delete_file: failed to read dir sector\n");
        return false;
    }

    // 3. Mark any preceding LFN entries (within the same sector) as deleted
    pt::uint32_t off = st->dir_entry_offset;
    while (off >= 32) {
        off -= 32;
        FAT32_DirEntry* e = (FAT32_DirEntry*)(fat32_sector_buf + off);
        if (e->attributes != FAT32_ATTR_LFN) break;
        fat32_sector_buf[off] = 0xE5;
    }

    // 4. Mark the 8.3 entry itself as deleted
    fat32_sector_buf[st->dir_entry_offset] = 0xE5;

    if (!Disk::write_sector(st->dir_entry_sector, fat32_sector_buf)) {
        klog("[FAT32] delete_file: failed to write dir sector\n");
        return false;
    }

    klog("[FAT32] delete_file: deleted '%s'\n", filename);
    return true;
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
