#pragma once

#include "defs.h"

enum class FdType : pt::uint8_t {
    FILE     = 0,   // FAT12 filesystem file (default; zero-init is correct)
    PIPE_RD  = 1,   // pipe read end
    PIPE_WR  = 2,   // pipe write end
    TCP_SOCK = 3,   // TCP socket (pointer to TcpSocket stored in fs_data)
    UDP_SOCK = 4,   // UDP socket (pointer to UdpSocket stored in fs_data)
};

// Result structure for stat_file: file size + FAT timestamps.
struct StatResult {
    pt::uint32_t file_size;
    pt::uint16_t create_time;
    pt::uint16_t create_date;
    pt::uint16_t modify_time;
    pt::uint16_t modify_date;
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
    // write_file / open_file_write default to "not supported" so FAT12 needs no changes.
    virtual pt::uint32_t write_file(File*, const void*, pt::uint32_t) { return 0; }
    virtual bool open_file_write(const char*, File*) { return false; }
    virtual bool open_file_readwrite(const char*, File*) { return false; }
    virtual bool create_directory(const char*) { return false; }
    virtual pt::uint32_t seek_file(File* file, pt::int32_t offset, int whence) = 0;
    virtual void close_file(File* file) = 0;
    virtual bool file_exists(const char* filename) = 0;
    virtual void list_root_directory() = 0;
    virtual bool create_file(const char* filename, const pt::uint8_t* data, pt::uint32_t size) = 0;
    virtual bool delete_file(const char* filename) = 0;
    // readdir: fill name_out and *size_out for the idx-th regular file (0-based).
    // Returns true if found, false if idx >= file count.
    virtual bool readdir(int /*idx*/, char* /*name_out*/, pt::uint32_t* /*size_out*/) { return false; }
    // readdir_ex: enumerate files AND directories under 'path' (NULL/"" = root).
    // Returns 1=found, 0=end.  type_out: 0=file, 1=dir.
    virtual int readdir_ex(const char* /*path*/, int /*idx*/, char* /*name_out*/,
                           pt::uint32_t* /*size_out*/, pt::uint8_t* /*type_out*/) { return 0; }
    virtual bool stat_file(const char* /*filename*/, StatResult* /*out*/) { return false; }
    virtual void list_directory(const char* /*path*/) {}
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
    static pt::uint32_t write_file(File* file, const void* buffer, pt::uint32_t bytes_to_write);
    static bool open_file_write(const char* filename, File* out);
    static bool open_file_readwrite(const char* filename, File* out);
    static bool create_directory(const char* path);
    static pt::uint32_t seek_file(File* file, pt::int32_t offset, int whence);
    static void close_file(File* file);
    static bool file_exists(const char* filename);
    static void list_root_directory();
    static bool create_file(const char* filename, const pt::uint8_t* data, pt::uint32_t size);
    static bool delete_file(const char* filename);
    static bool readdir(int idx, char* name_out, pt::uint32_t* size_out);
    static int  readdir_ex(const char* path, int idx, char* name_out,
                           pt::uint32_t* size_out, pt::uint8_t* type_out);
    static bool stat_file(const char* filename, StatResult* out);
    static void list_directory(const char* path);
    static pt::uint32_t get_bytes_per_cluster();
    static pt::uint32_t get_free_space();
    static pt::uint32_t get_total_space();

    static Filesystem* active_fs;
};
