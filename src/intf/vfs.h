#pragma once

#include "defs.h"

enum class FdType : pt::uint8_t {
    FILE    = 0,   // FAT12 filesystem file (default; zero-init is correct)
    PIPE_RD = 1,   // pipe read end
    PIPE_WR = 2,   // pipe write end
};

// Generic file handle - replaces FAT12_File everywhere.
// fs_data is opaque FS-private state (e.g. FAT12State).
struct File {
    char         filename[13];
    pt::uint32_t file_size;
    pt::uint32_t current_position;
    bool         open;
    FdType       type;             // FILE, PIPE_RD, or PIPE_WR
    pt::uint8_t  fs_data[32];  // opaque FS-private state
};

// Abstract filesystem interface.
class Filesystem {
public:
    virtual bool mount() = 0;
    virtual bool open_file(const char* filename, File* file) = 0;
    virtual pt::uint32_t read_file(File* file, void* buffer, pt::uint32_t bytes_to_read) = 0;
    virtual void close_file(File* file) = 0;
    virtual bool file_exists(const char* filename) = 0;
    virtual void list_root_directory() = 0;
    virtual bool create_file(const char* filename, const pt::uint8_t* data, pt::uint32_t size) = 0;
    virtual bool delete_file(const char* filename) = 0;
    virtual pt::uint32_t get_bytes_per_cluster() = 0;
    virtual pt::uint32_t get_free_space() = 0;
    virtual pt::uint32_t get_total_space() = 0;
};

// VFS static facade - same API as Filesystem, delegates to active_fs.
class VFS {
public:
    static bool mount();
    static bool open_file(const char* filename, File* file);
    static pt::uint32_t read_file(File* file, void* buffer, pt::uint32_t bytes_to_read);
    static void close_file(File* file);
    static bool file_exists(const char* filename);
    static void list_root_directory();
    static bool create_file(const char* filename, const pt::uint8_t* data, pt::uint32_t size);
    static bool delete_file(const char* filename);
    static pt::uint32_t get_bytes_per_cluster();
    static pt::uint32_t get_free_space();
    static pt::uint32_t get_total_space();

    static Filesystem* active_fs;
};
