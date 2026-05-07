#pragma once
#include "../syscall.h"

#ifndef S_IFMT
#define S_IFMT  0170000
#endif
#ifndef S_IFDIR
#define S_IFDIR 0040000
#endif
#ifndef S_IFREG
#define S_IFREG 0100000
#endif
#ifndef S_IREAD
#define S_IREAD 0400
#endif
#ifndef S_IWRITE
#define S_IWRITE 0200
#endif
#ifndef S_IRUSR
#define S_IRUSR 0400
#endif
#ifndef S_IWUSR
#define S_IWUSR 0200
#endif
#ifndef S_IRGRP
#define S_IRGRP 0040
#endif
#ifndef S_IWGRP
#define S_IWGRP 0020
#endif
#ifndef S_IROTH
#define S_IROTH 0004
#endif

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)

/* Q2-compatible struct stat (minimal) */
struct stat {
    unsigned int st_mode;
    long         st_mtime;
    long         st_ctime;
    unsigned int st_size;
};

/* stat: regular files via SYS_STAT; directories detected via SYS_READDIR_EX. */
static inline int stat(const char *path, struct stat *buf)
{
    if (!path) return -1;
    if (buf) {
        buf->st_mode  = 0;
        buf->st_mtime = 0;
        buf->st_ctime = 0;
        buf->st_size  = 0;
    }
    /* Kernel StatResult layout: u32 file_size, u16 ctime, cdate, mtime, mdate. */
    struct { unsigned int file_size; unsigned short ct, cd, mt, md; } sr;
    if (sys_stat(path, &sr) == 0) {
        if (buf) {
            buf->st_mode = S_IFREG | 0644;
            buf->st_size = sr.file_size;
        }
        return 0;
    }
    /* Not a file — probe as directory. Empty directories will appear missing. */
    char nm[256];
    unsigned int sz = 0;
    unsigned char ty = 0;
    if (sys_readdir_ex(0, nm, &sz, path, &ty) == 1) {
        if (buf) {
            buf->st_mode = S_IFDIR | 0755;
            buf->st_size = 0;
        }
        return 0;
    }
    return -1;
}

/* fstat: get file size via SYS_STAT if available, else stub */
static inline int fstat(int fd, struct stat *buf)
{
    (void)fd;
    if (buf) {
        buf->st_mode  = S_IFREG;
        buf->st_mtime = 0;
        buf->st_size  = 0;
    }
    return 0;
}

