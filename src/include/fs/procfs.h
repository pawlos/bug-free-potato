#pragma once
#include "defs.h"
#include "fs/vfs.h"

// ProcFS — synthetic read-only virtual filesystem mounted at "proc/".
//
// open_file() generates the full file content into a kmalloc'd buffer and
// stores (ptr, size) in File::fs_data[0..11].  read_file/seek_file work
// like a flat buffer; close_file kfree's it.
class ProcFS : public Filesystem {
public:
    // mount() reads the FAT32 boot sector to extract the volume label.
    bool mount() override;

    bool open_file(const char* path, File* file) override;
    pt::uint32_t read_file(File* file, void* buf, pt::uint32_t n) override;
    pt::uint32_t seek_file(File* file, pt::int32_t offset, int whence) override;
    void close_file(File* file) override;

    bool file_exists(const char* path) override;

    // readdir_ex: enumerate proc directory entries.
    // "proc" root -> system files then one dir entry per live task.
    // "proc/<pid>" -> status, maps, syscalls.
    int readdir_ex(const char* path, int idx,
                   char* name_out, pt::uint32_t* size_out,
                   pt::uint8_t* type_out) override;

    void list_directory(const char* path) override;

    // Stubs for write-side Filesystem interface (proc is read-only).
    bool create_file(const char*, const pt::uint8_t*, pt::uint32_t) override { return false; }
    bool delete_file(const char*) override { return false; }
    void list_root_directory() override {}
    pt::uint32_t get_bytes_per_cluster() override { return 0; }
    pt::uint32_t get_free_space()  override { return 0; }
    pt::uint32_t get_total_space() override { return 0; }

private:
    char volume_label[12];   // null-terminated, read from FAT32 BPB at mount()

    // Content generator helpers — write into caller-supplied buffer, return bytes written.
    int gen_version (char* buf, int cap);
    int gen_meminfo (char* buf, int cap);
    int gen_uptime  (char* buf, int cap);
    int gen_status  (pt::uint32_t pid, char* buf, int cap);
    int gen_maps    (pt::uint32_t pid, char* buf, int cap);
    int gen_syscalls(pt::uint32_t pid, char* buf, int cap);

    // Parse "<pid>/file" path -> sets *pid_out and returns pointer to "file" part.
    // Returns nullptr if the first segment is not a decimal number.
    static const char* parse_pid_path(const char* path, pt::uint32_t* pid_out);

    // Install generated content into File handle (kmalloc's buffer, sets type).
    bool install_content(File* file, const char* buf, int len);
};
